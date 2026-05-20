#include <algorithm>
#include "common_utils.hpp"

namespace FileSysUtils
{

std::string read_file(const std::string &filename)
{
    std::ifstream file(filename);
    if (!file.is_open())
    {
        throw std::runtime_error("Could not open file: " + filename);
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

bool ensure_directory_exists(const std::string &path)
{
    fs::path dir_path(path);
    fs::path target_dir;

    // If the path has an extension, treat it as a file and get its parent directory
    // If no extension, treat the entire path as a directory
    if (dir_path.has_extension())
    {
        target_dir = dir_path.parent_path();
    }
    else
    {
        target_dir = dir_path;
    }

    if (fs::exists(target_dir))
    {
        return fs::is_directory(target_dir);
    }

    return fs::create_directories(target_dir);
}

std::string join_path(const std::string &base, const std::string &relative)
{
    if (base.empty())
        return relative;
    if (relative.empty())
        return base;

    std::string result = base;

    // Ensure base doesn't end with '/'
    if (result.back() == '/')
    {
        result.pop_back();
    }

    // Ensure relative doesn't start with '/'
    std::string rel = relative;
    if (rel.front() == '/')
    {
        rel = rel.substr(1);
    }

    return result + "/" + rel;
}

std::string join_path_and_file_name(const std::string &base, const std::string &filename)
{
    return (fs::path(base) / fs::path(filename).filename()).string();
}

std::string extract_file_name(const std::string &full_path)
{
    return fs::path(full_path).filename().string();
}

int move_file_sendfile(const std::string &src, const std::string &dst)
{
    int src_fd = open(src.c_str(), O_RDONLY);
    if (src_fd < 0)
    {
        perror("open source");
        return -1;
    }

    // Get file size
    struct stat st;
    if (fstat(src_fd, &st) < 0)
    {
        perror("fstat");
        close(src_fd);
        return -1;
    }

    // Open destination with optimal flags
    int dst_fd = open(dst.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (dst_fd < 0)
    {
        perror("open destination");
        close(src_fd);
        return -1;
    }

    // Zero-copy transfer in kernel space
    off_t offset = 0;
    ssize_t sent = sendfile(dst_fd, src_fd, &offset, st.st_size);

    close(src_fd);
    close(dst_fd);

    if (sent != st.st_size)
    {
        perror("sendfile incomplete");
        unlink(dst.c_str()); // Remove partial file
        return -1;
    }

    // Delete source file
    if (unlink(src.c_str()) < 0)
    {
        perror("unlink source");
        return -1;
    }

    return 0;
}

std::size_t delete_files(const std::vector<std::string> &file_paths)
{
    std::size_t deleted_count = 0;

    for (const auto &file_path : file_paths)
    {
        if (file_path.empty())
        {
            std::cout << __FUNCTION__ << " Warning: Empty file path provided" << std::endl;
            continue;
        }

        try
        {
            fs::path path(file_path);

            if (!fs::exists(path))
            {
                std::cout << __FUNCTION__ << " Warning: File does not exist: " << file_path << std::endl;
                continue;
            }

            if (!fs::is_regular_file(path))
            {
                std::cout << __FUNCTION__ << " Warning: Path is not a regular file: " << file_path << std::endl;
                continue;
            }

            if (fs::remove(path))
            {
                deleted_count++;
            }
            else
            {
                std::cerr << __FUNCTION__ << " Error: Failed to delete file: " << file_path << std::endl;
            }
        }
        catch (const fs::filesystem_error &ex)
        {
            std::cerr << __FUNCTION__ << " Filesystem error deleting file '" << file_path << "': " << ex.what()
                      << std::endl;
        }
        catch (const std::exception &ex)
        {
            std::cerr << __FUNCTION__ << " Error deleting file '" << file_path << "': " << ex.what() << std::endl;
        }
    }

    return deleted_count;
}

std::vector<std::string> get_all_file_names(const std::string &dir_path, bool include_path,
                                            const std::string &with_name_prefix)
{
    std::vector<std::string> file_names;
    try
    {
        fs::path path(dir_path);
        if (!fs::exists(path) || !fs::is_directory(path))
        {
            std::cerr << "Error: Path is not a valid directory: " << dir_path << std::endl;
            return file_names;
        }
        for (const auto &entry : fs::directory_iterator(path))
        {
            if (fs::is_regular_file(entry.path()))
            {
                std::string filename = entry.path().filename().string();

                // Check if filename matches the prefix (if prefix is provided)
                if (!with_name_prefix.empty() && filename.find(with_name_prefix) != 0)
                {
                    continue; // Skip files that don't start with the prefix
                }

                if (include_path)
                {
                    file_names.push_back(entry.path().string());
                }
                else
                {
                    file_names.push_back(filename);
                }
            }
        }
    }
    catch (const fs::filesystem_error &ex)
    {
        std::cerr << "Filesystem error reading directory '" << dir_path << "': " << ex.what() << std::endl;
    }

    std::sort(file_names.begin(), file_names.end());

    return file_names;
}

} // namespace FileSysUtils

namespace CodecUtils
{

CodecType detect_codec_type(const std::string &filePath)
{
    AVFormatContext *fmt_ctx = nullptr;
    av_log_set_level(AV_LOG_QUIET); // silence FFmpeg

    AVDictionary *opts = nullptr;
    av_dict_set(&opts, "analyzeduration", "100000", 0); // 0.1s
    av_dict_set(&opts, "probesize", "500000", 0);       // 0.5MB

    if (avformat_open_input(&fmt_ctx, filePath.c_str(), nullptr, &opts) < 0)
    {
        av_dict_free(&opts);
        return CodecType::UNKNOWN;
    }
    av_dict_free(&opts);

    for (unsigned int i = 0; i < fmt_ctx->nb_streams; ++i)
    {
        const AVStream *stream = fmt_ctx->streams[i];
        if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            switch (stream->codecpar->codec_id)
            {
            case AV_CODEC_ID_H264:
                avformat_close_input(&fmt_ctx);
                return CodecType::H264;
            case AV_CODEC_ID_HEVC:
                avformat_close_input(&fmt_ctx);
                return CodecType::H265;
            default:
                avformat_close_input(&fmt_ctx);
                return CodecType::UNKNOWN;
            }
        }
    }

    avformat_close_input(&fmt_ctx);
    return CodecType::UNKNOWN;
}
} // namespace CodecUtils

namespace SystemUtils
{

std::shared_ptr<uint8_t> page_aligned_alloc(size_t size)
{
    auto addr = mmap(NULL, size, PROT_WRITE | PROT_READ, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (MAP_FAILED == addr)
        throw std::bad_alloc();
    return std::shared_ptr<uint8_t>(reinterpret_cast<uint8_t *>(addr), [size](void *addr) { munmap(addr, size); });
}

unsigned long long getTotalMemoryBytes()
{
    struct sysinfo info;

    if (sysinfo(&info) != 0)
    {
        return 0; // Error
    }

    return info.totalram * info.mem_unit;
}

double getTotalMemoryGB()
{
    unsigned long long bytes = getTotalMemoryBytes();
    if (bytes == 0)
    {
        return 0.0;
    }

    return bytes / (1024.0 * 1024.0 * 1024.0);
}

} // namespace SystemUtils

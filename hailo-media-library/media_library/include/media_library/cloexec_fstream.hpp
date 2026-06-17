#pragma once

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ext/stdio_filebuf.h>
#include <ios>
#include <istream>
#include <ostream>
#include <string>
#include <utility>

// Drop-in replacements for std::ofstream / std::ifstream that open the
// underlying fd with O_CLOEXEC atomically, so the fd does not leak into
// child processes via fork+exec. std::basic_filebuf gives no way to pass
// O_CLOEXEC at open time, which is why these wrappers exist.
namespace cloexec
{

namespace detail
{

inline int open_for_write(const std::string &path, std::ios::openmode mode)
{
    int flags = O_CLOEXEC | O_WRONLY | O_CREAT;
    if ((mode & std::ios::app) != 0)
    {
        flags |= O_APPEND;
    }
    else
    {
        // std's fopen_mode maps `out` (with or without ate or trunc) to "w",
        // which always truncates. ate is a separate seek-to-end step.
        flags |= O_TRUNC;
    }
    // 0666 matches fopen (which is what std::basic_filebuf::open calls). The
    // process umask is applied by ::open, so typical umask 022 still yields
    // 0644 on disk.
    constexpr mode_t DEFAULT_PERMS = 0666;
    // ::open is the POSIX open(2) from <fcntl.h>; the leading :: disambiguates
    // from the in-class ofstream::open / ifstream::open members below.
    int fd = ::open(path.c_str(), flags, DEFAULT_PERMS);
    // std::basic_filebuf::open does seekoff(0,end) for ate; stdio_filebuf
    // does not. We replicate it. With O_TRUNC the seek is a no-op (file is
    // empty); O_APPEND already places writes at EOF, so skip there.
    if (fd >= 0 && (mode & std::ios::ate) != 0 && (mode & std::ios::app) == 0)
    {
        ::lseek(fd, 0, SEEK_END);
    }
    return fd;
}

inline int open_for_read(const std::string &path, std::ios::openmode mode)
{
    int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd >= 0 && (mode & std::ios::ate) != 0)
    {
        ::lseek(fd, 0, SEEK_END);
    }
    return fd;
}

} // namespace detail

// The stdio_filebuf is held by value (not unique_ptr) so it lives at a
// fixed address inside the wrapper. That matches std::basic_[io]fstream's
// layout: rdbuf() is always non-null, default-constructed state is goodbit,
// and move semantics can mirror the standard's set_rdbuf-based pattern
// instead of going through rdbuf(streambuf*) which would clear() the state.

class ofstream : public std::ostream
{
  public:
    ofstream() : std::ostream(nullptr)
    {
        this->init(&m_buf);
    }

    explicit ofstream(const std::string &path, std::ios::openmode mode = std::ios::out) : ofstream()
    {
        open(path, mode);
    }

    explicit ofstream(const char *path, std::ios::openmode mode = std::ios::out) : ofstream()
    {
        open(path, mode);
    }

    ~ofstream() override = default;

    ofstream(const ofstream &) = delete;
    ofstream &operator=(const ofstream &) = delete;

    ofstream(ofstream &&other) : std::ostream(std::move(other)), m_buf(std::move(other.m_buf))
    {
        this->set_rdbuf(&m_buf);
    }

    ofstream &operator=(ofstream &&other)
    {
        std::ostream::operator=(std::move(other));
        m_buf = std::move(other.m_buf);
        return *this;
    }

    void swap(ofstream &other)
    {
        std::ostream::swap(other);
        m_buf.swap(other.m_buf);
    }

    void open(const std::string &path, std::ios::openmode mode = std::ios::out)
    {
        // std::basic_filebuf::open immediately returns null if a file is
        // already open, and std::basic_ofstream::open turns that into
        // failbit. Match that — silently closing the prior file would
        // also swallow any close-failure (lost-write) error.
        if (m_buf.is_open())
        {
            setstate(std::ios::failbit);
            return;
        }
        int fd = detail::open_for_write(path, mode);
        if (fd < 0)
        {
            setstate(std::ios::failbit);
            return;
        }
        __gnu_cxx::stdio_filebuf<char> new_buf(fd, mode | std::ios::out);
        if (!new_buf.is_open())
        {
            ::close(fd);
            setstate(std::ios::failbit);
            return;
        }
        m_buf = std::move(new_buf);
        this->clear();
    }

    void open(const char *path, std::ios::openmode mode = std::ios::out)
    {
        open(std::string(path), mode);
    }

    bool is_open() const
    {
        return m_buf.is_open();
    }

    void close()
    {
        if (!m_buf.close())
        {
            setstate(std::ios::failbit);
        }
    }

  private:
    __gnu_cxx::stdio_filebuf<char> m_buf;
};

class ifstream : public std::istream
{
  public:
    ifstream() : std::istream(nullptr)
    {
        this->init(&m_buf);
    }

    explicit ifstream(const std::string &path, std::ios::openmode mode = std::ios::in) : ifstream()
    {
        open(path, mode);
    }

    explicit ifstream(const char *path, std::ios::openmode mode = std::ios::in) : ifstream()
    {
        open(path, mode);
    }

    ~ifstream() override = default;

    ifstream(const ifstream &) = delete;
    ifstream &operator=(const ifstream &) = delete;

    ifstream(ifstream &&other) : std::istream(std::move(other)), m_buf(std::move(other.m_buf))
    {
        this->set_rdbuf(&m_buf);
    }

    ifstream &operator=(ifstream &&other)
    {
        std::istream::operator=(std::move(other));
        m_buf = std::move(other.m_buf);
        return *this;
    }

    void swap(ifstream &other)
    {
        std::istream::swap(other);
        m_buf.swap(other.m_buf);
    }

    void open(const std::string &path, std::ios::openmode mode = std::ios::in)
    {
        if (m_buf.is_open())
        {
            setstate(std::ios::failbit);
            return;
        }
        int fd = detail::open_for_read(path, mode);
        if (fd < 0)
        {
            setstate(std::ios::failbit);
            return;
        }
        __gnu_cxx::stdio_filebuf<char> new_buf(fd, mode | std::ios::in);
        if (!new_buf.is_open())
        {
            ::close(fd);
            setstate(std::ios::failbit);
            return;
        }
        m_buf = std::move(new_buf);
        this->clear();
    }

    void open(const char *path, std::ios::openmode mode = std::ios::in)
    {
        open(std::string(path), mode);
    }

    bool is_open() const
    {
        return m_buf.is_open();
    }

    void close()
    {
        if (!m_buf.close())
        {
            setstate(std::ios::failbit);
        }
    }

  private:
    __gnu_cxx::stdio_filebuf<char> m_buf;
};

inline void swap(ofstream &a, ofstream &b)
{
    a.swap(b);
}

inline void swap(ifstream &a, ifstream &b)
{
    a.swap(b);
}

} // namespace cloexec

#include <CLI/CLI.hpp>
#include <iostream>
#include "osd.hpp"

int main(int argc, char *argv[])
{
    CLI::App app{"Calculate text size for OSD"};

    std::string label, font_path;
    int font_size, line_thickness;

    app.add_option("-l,--label", label, "Label to calculate size for", true);
    app.add_option("-f,--font", font_path, "Font file path to use", true);
    app.add_option("-s,--size", font_size, "Font size in pixels", true);
    app.add_option("-t,--thickness", line_thickness, "Line thickness like in opencv", true);

    CLI11_PARSE(app, argc, argv);

    mat_dims dims = osd::calculate_text_size(label, font_path, font_size, line_thickness);

    std::cout << "Required width: " << dims.width << ", height: " << dims.height << std::endl;
    return 0;
}

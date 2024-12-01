#include "media_library/media_library_types.hpp"
#include "media_library/privacy_mask.hpp"
#include "media_library/privacy_mask_types.hpp"

#include "opencv2/imgproc.hpp"
#include "opencv2/imgcodecs.hpp"
#include "opencv2/highgui.hpp"
#include "opencv2/core/core.hpp"

#include <vector>
#include <string>
#include <iostream>

using namespace privacy_mask_types;

static void init_vertices_1(polygon &polygon)
{
    polygon.vertices.push_back(vertex(125, 25));
    polygon.vertices.push_back(vertex(1600, 25));
    polygon.vertices.push_back(vertex(2120, 1200));
    polygon.vertices.push_back(vertex(3144, 1923));
    polygon.vertices.push_back(vertex(900, 700));
    polygon.vertices.push_back(vertex(125, 1923));
}

static void init_vertices_2(polygon &polygon)
{
    polygon.vertices.push_back(vertex(2500, 70));
    polygon.vertices.push_back(vertex(2980, 70));
    polygon.vertices.push_back(vertex(2900, 550));
    polygon.vertices.push_back(vertex(2723, 550));
    polygon.vertices.push_back(vertex(2600, 120));
}

static void init_vertices_3(polygon &polygon)
{
    polygon.vertices.push_back(vertex(2500, 970));
    polygon.vertices.push_back(vertex(2980, 970));
    polygon.vertices.push_back(vertex(2900, 1450));
    polygon.vertices.push_back(vertex(2723, 1450));
    polygon.vertices.push_back(vertex(2540, 1450));
}

static void init_vertices_4(polygon &polygon)
{
    polygon.vertices.push_back(vertex(10, 1990));
    polygon.vertices.push_back(vertex(3500, 1990));
    polygon.vertices.push_back(vertex(3500, 2100));
    polygon.vertices.push_back(vertex(10, 2100));
}

int main(int argc, char **argv)
{
    cv::Mat src;
    cv::CommandLineParser parser(argc, argv, "{@input | test.jpg | input image}");

    cv::String input_image = parser.get<cv::String>("@input");
    src = cv::imread(cv::samples::findFile(input_image));

    if (src.empty())
    {
        printf("Error opening image: %s\n", input_image.c_str());
        return 0;
    }

    polygon example_polygon;
    example_polygon.id = "polygon1";
    init_vertices_1(example_polygon);

    polygon example_polygon2;
    example_polygon2.id = "polygon2";
    init_vertices_2(example_polygon2);

    polygon example_polygon3;
    example_polygon3.id = "polygon3";
    init_vertices_3(example_polygon3);

    polygon example_polygon4;
    example_polygon4.id = "polygon4";
    init_vertices_4(example_polygon4);

    tl::expected<std::shared_ptr<PrivacyMaskBlender>, media_library_return> blender_expected =
        PrivacyMaskBlender::create(src.cols, src.rows);
    if (!blender_expected.has_value())
    {
        return 1;
    }

    PrivacyMaskBlenderPtr privacy_mask_blender = blender_expected.value();
    privacy_mask_blender->set_color({23, 161, 231});

    privacy_mask_blender->add_privacy_mask(example_polygon);
    privacy_mask_blender->add_privacy_mask(example_polygon2);
    privacy_mask_blender->add_privacy_mask(example_polygon3);
    privacy_mask_blender->add_privacy_mask(example_polygon4);

    privacy_mask_blender->blend();

    return 0;
}

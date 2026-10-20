#include <cmath>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "test_support.hpp"
#include "circle_generator.hpp"
#include "designer_stroke_generator.hpp"
#include "multi_image_layout.hpp"
#include "rect_generator.hpp"
#include "rng.hpp"
#include "stats.hpp"
#include "transform.hpp"
namespace
{

void check_rect(const bench::RectData& actual, const bench::RectData& expected)
{
    CHECK_NEAR(actual.x, expected.x, 0.00002);
    CHECK_NEAR(actual.y, expected.y, 0.00002);
    CHECK_NEAR(actual.w, expected.w, 0.00002);
    CHECK_NEAR(actual.h, expected.h, 0.00002);
    CHECK(actual.r == expected.r);
    CHECK(actual.g == expected.g);
    CHECK(actual.b == expected.b);
    CHECK(actual.a == expected.a);
}

void check_circle(const bench::CircleData& actual,
                  const bench::CircleData& expected)
{
    CHECK_NEAR(actual.cx, expected.cx, 0.00002);
    CHECK_NEAR(actual.cy, expected.cy, 0.00002);
    CHECK_NEAR(actual.radius, expected.radius, 0.00002);
    CHECK(actual.r == expected.r);
    CHECK(actual.g == expected.g);
    CHECK(actual.b == expected.b);
    CHECK(actual.a == expected.a);
}

void test_stats()
{
    const auto empty = bench::compute_stats({});
    CHECK(empty.avg_ms == 0.0);
    CHECK(empty.median_ms == 0.0);
    CHECK(empty.p95_ms == 0.0);
    CHECK(empty.p99_ms == 0.0);
    CHECK(empty.min_ms == 0.0);
    CHECK(empty.max_ms == 0.0);
    CHECK(empty.stddev_ms == 0.0);
    CHECK(empty.fps == 0.0);
    CHECK(bench::percentile({}, 50.0) == 0.0);
    CHECK(bench::percentile({12.0}, 99.0) == 12.0);
    CHECK(bench::percentile({2.0, 4.0, 10.0}, 0.0) == 2.0);
    CHECK(bench::percentile({2.0, 4.0, 10.0}, 50.0) == 4.0);
    CHECK(bench::percentile({2.0, 4.0, 10.0}, 100.0) == 10.0);
    CHECK(bench::percentile({2.0, 4.0, 10.0}, 75.0) == 7.0);

    const std::vector<double> samples = {9.0, 1.0, 5.0, 3.0, 7.0};
    const auto stats = bench::compute_stats(samples);
    CHECK(stats.avg_ms == 5.0);
    CHECK(stats.median_ms == 5.0);
    CHECK_NEAR(stats.p95_ms, 8.6, 1e-12);
    CHECK_NEAR(stats.p99_ms, 8.92, 1e-12);
    CHECK(stats.min_ms == 1.0);
    CHECK(stats.max_ms == 9.0);
    CHECK_NEAR(stats.stddev_ms, std::sqrt(8.0), 1e-12);
    CHECK(stats.fps == 200.0);
    CHECK(samples.front() == 9.0);

    const auto even = bench::compute_stats({10.0, 40.0, 20.0, 30.0});
    CHECK(even.median_ms == 25.0);
    CHECK(even.avg_ms == 25.0);
    CHECK_NEAR(even.stddev_ms, std::sqrt(125.0), 1e-12);
    CHECK(even.fps == 40.0);
    const auto one = bench::compute_stats({4.0});
    CHECK(one.avg_ms == 4.0);
    CHECK(one.min_ms == 4.0);
    CHECK(one.max_ms == 4.0);
    CHECK(one.median_ms == 4.0);
    CHECK(one.p95_ms == 4.0);
    CHECK(one.p99_ms == 4.0);
    CHECK(one.stddev_ms == 0.0);
    CHECK(one.fps == 250.0);
    const auto zero = bench::compute_stats({0.0, 0.0});
    CHECK(zero.avg_ms == 0.0);
    CHECK(zero.stddev_ms == 0.0);
    CHECK(zero.fps == 0.0);
}

void test_rng()
{
    // Published PCG32 reference vector: initstate=42, initseq=54.
    const uint32_t reference[] = {0xa15c02b7U, 0x7b47f409U, 0xba1d3330U,
                                  0x83d2f293U, 0xbfa4784bU, 0xcbed606eU};
    bench::PCG32 rng(42, 54);
    for (auto value : reference)
        CHECK(rng.next_u32() == value);
    rng.seed_with(42, 54);
    CHECK(rng.next_u32() == reference[0]);
    bench::PCG32 defaults;
    bench::PCG32 explicit_defaults(12345, 1);
    for (int i = 0; i < 16; ++i)
        CHECK(defaults.next_u32() == explicit_defaults.next_u32());

    rng.seed_with(42, 54);
    CHECK_NEAR(rng.next_float_01(), 0.6303102374, 1e-7);
    rng.seed_with(42, 54);
    CHECK_NEAR(rng.next_float(-2.0f, 6.0f), 3.0424818993, 1e-6);
    CHECK(rng.next_float(3.5f, 3.5f) == 3.5f);
    rng.seed_with(42, 54);
    CHECK(rng.next_int(-4, 4) == 2);
    CHECK(rng.next_int(-4, 4) == -1);
    CHECK(rng.next_int(7, 7) == 7);
    rng.seed_with(42, 54);
    CHECK(rng.next_int(8, 3) == 8);
    CHECK(rng.next_u32() == reference[0]);
    rng.seed_with(42, 54);
    CHECK(rng.next_u8(17, 29) == 18);
    CHECK(rng.next_u8(255, 255) == 255);
    rng.seed_with(42, 54);
    CHECK(rng.next_u8(29, 17) == 29);
    CHECK(rng.next_u32() == reference[0]);
    rng.seed_with(42);
    CHECK(rng.next_u32() == 0x4df1ccf9U);
}

void test_rectangles()
{
    bench::RectGenConfig config;
    config.canvas_width = 64;
    config.canvas_height = 32;
    config.rect_count = 128;
    config.min_rect_size = 8.0f;
    config.max_rect_size = 24.0f;
    config.alpha_min = 17;
    config.alpha_max = 29;
    const auto rects = bench::generate_rects(42, 0, config);
    const auto static_rects = bench::generate_static_rects(42, config);
    const auto frame3 = bench::generate_rects(42, 3, config);
    CHECK(rects.size() == 128);
    CHECK(static_rects.size() == rects.size());
    CHECK(frame3.size() == 128);
    if (!rects.empty())
        check_rect(rects[0], {15.7962818f, 19.3137684f, 12.8715334f, 22.3446121f, 116, 168, 30, 26});
    if (!frame3.empty())
        check_rect(frame3[0], {9.8516054f, 19.3515739f, 16.7889004f, 22.4119263f, 132, 182, 23, 29});
    for (size_t i = 0; i < rects.size(); ++i) {
        const auto& r = rects[i];
        CHECK(r.w >= 8.0f && r.w <= 24.0f);
        CHECK(r.h >= 8.0f && r.h <= 24.0f);
        CHECK(r.x >= -r.w * 0.5f && r.x <= 64.0f - r.w * 0.5f);
        CHECK(r.y >= -r.h * 0.5f && r.y <= 32.0f - r.h * 0.5f);
        CHECK(r.a >= 17 && r.a <= 29);
        if (i < static_rects.size())
            check_rect(r, static_rects[i]);
    }
    config.rect_count = 0;
    CHECK(bench::generate_rects(42, 0, config).empty());
    config.rect_count = 2;
    config.canvas_width = config.canvas_height = 0;
    config.min_rect_size = config.max_rect_size = 12.0f;
    config.alpha_min = config.alpha_max = 255;
    for (const auto& r : bench::generate_rects(42, 0, config)) {
        CHECK(r.w == 12.0f && r.h == 12.0f);
        CHECK(r.x == -6.0f && r.y == -6.0f);
        CHECK(r.a == 255);
    }
}

void test_circles()
{
    bench::CircleGenConfig config;
    config.canvas_width = 64;
    config.canvas_height = 32;
    config.circle_count = 128;
    config.min_diameter = 8.0f;
    config.max_diameter = 24.0f;
    config.alpha_min = 17;
    config.alpha_max = 29;
    const auto circles = bench::generate_circles(42, 0, config);
    const auto static_circles = bench::generate_static_circles(42, config);
    const auto frame3 = bench::generate_circles(42, 3, config);
    CHECK(circles.size() == 128);
    CHECK(static_circles.size() == circles.size());
    CHECK(frame3.size() == 128);
    if (!circles.empty())
        check_circle(circles[0], {57.3784447f, 11.1160240f, 6.4357667f, 81, 116, 168, 21});
    if (!frame3.empty())
        check_circle(frame3[0], {57.6477089f, 9.1230278f, 8.3944502f, 1, 132, 182, 28});
    for (size_t i = 0; i < circles.size(); ++i) {
        const auto& c = circles[i];
        CHECK(c.radius >= 4.0f && c.radius <= 12.0f);
        CHECK(c.cx >= 0.0f && c.cx <= 64.0f);
        CHECK(c.cy >= 0.0f && c.cy <= 32.0f);
        CHECK(c.a >= 17 && c.a <= 29);
        if (i < static_circles.size())
            check_circle(c, static_circles[i]);
    }
    config.circle_count = 0;
    CHECK(bench::generate_circles(42, 0, config).empty());
    config.circle_count = 2;
    config.canvas_width = config.canvas_height = 0;
    config.min_diameter = config.max_diameter = 12.0f;
    config.alpha_min = config.alpha_max = 255;
    for (const auto& c : bench::generate_circles(42, 0, config)) {
        CHECK(c.radius == 6.0f);
        CHECK(c.cx == 0.0f && c.cy == 0.0f);
        CHECK(c.a == 255);
    }
}

void test_transforms()
{
    bench::TransformGenConfig config;
    config.max_translation = 7.0f;
    config.max_rotation_deg = 60.0f;
    config.min_scale = 0.5f;
    config.max_scale = 2.0f;
    const auto transforms = bench::generate_transforms(42, 0, 128, config);
    const auto repeat = bench::generate_transforms(42, 0, 128, config);
    const auto frame3 = bench::generate_transforms(42, 3, 1, config);
    CHECK(transforms.size() == 128);
    CHECK(repeat.size() == transforms.size());
    CHECK(frame3.size() == 1);
    if (!transforms.empty()) {
        CHECK_NEAR(transforms[0].dx, 4.8729553, 0.00002);
        CHECK_NEAR(transforms[0].dy, 5.7838345, 0.00002);
        CHECK_NEAR(transforms[0].rotation_deg, 33.5457458, 0.00002);
        CHECK_NEAR(transforms[0].scale, 0.628683865, 0.000002);
    }
    if (!frame3.empty()) {
        CHECK_NEAR(frame3[0].dx, -6.6768389, 0.00002);
        CHECK_NEAR(frame3[0].dy, -0.7764139, 0.00002);
        CHECK_NEAR(frame3[0].rotation_deg, 1.0417252, 0.00002);
        CHECK_NEAR(frame3[0].scale, 1.57557988, 0.000002);
    }
    for (size_t i = 0; i < transforms.size(); ++i) {
        const auto& t = transforms[i];
        CHECK(t.dx >= -7.0f && t.dx <= 7.0f);
        CHECK(t.dy >= -7.0f && t.dy <= 7.0f);
        CHECK(t.rotation_deg >= -60.0f && t.rotation_deg <= 60.0f);
        CHECK(t.scale >= 0.5f && t.scale <= 2.0f);
        if (i < repeat.size()) {
            CHECK(t.dx == repeat[i].dx && t.dy == repeat[i].dy);
            CHECK(t.rotation_deg == repeat[i].rotation_deg);
            CHECK(t.scale == repeat[i].scale);
        }
    }
    CHECK(bench::generate_transforms(42, 0, 0).empty());
    config.max_translation = config.max_rotation_deg = 0.0f;
    config.min_scale = config.max_scale = 1.0f;
    for (const auto& t : bench::generate_transforms(42, 0, 2, config)) {
        CHECK(t.dx == 0.0f && t.dy == 0.0f);
        CHECK(t.rotation_deg == 0.0f);
        CHECK(t.scale == 1.0f);
    }
    CHECK_NEAR(bench::kDegToRad * 180.0f, 3.141592653589793, 1e-6);
}

void test_multi_image_layout()
{
    CHECK(bench::kMultiImageAssets.size() == 25);
    CHECK(bench::kMultiImageCount == 5000);
    std::set<std::string> assets(bench::kMultiImageAssets.begin(),
                                 bench::kMultiImageAssets.end());
    CHECK(assets.size() == 25);
    CHECK(std::string(bench::kMultiImageAssets[0]) == "kenney-animal-pack/PNG/Round/elephant.png");
    CHECK(std::string(bench::kMultiImageAssets[10]) == "kenney-animal-pack/PNG/Round (outline)/elephant.png");
    CHECK(std::string(bench::kMultiImageAssets[24]) == "kenney-animal-pack/PNG/Round without details/panda.png");
    const std::vector<uint32_t> expected = {10, 23, 23, 9, 23, 19, 14, 6, 13, 9};
    CHECK(bench::generate_multi_image_indices(42, 10) == expected);
    CHECK(bench::generate_multi_image_indices(42, 0).empty());
    const auto indices = bench::generate_multi_image_indices(42, 5000);
    CHECK(indices.size() == 5000);
    std::set<uint32_t> used;
    for (auto index : indices) {
        CHECK(index < 25);
        used.insert(index);
    }
    CHECK(used.size() == 25);
    const auto layout = bench::generate_multi_image_layout(42, 64, 32);
    CHECK(layout.size() == 5000);
    for (const auto& r : layout) {
        CHECK(r.w >= 10.0f && r.w <= 200.0f);
        CHECK(r.h >= 10.0f && r.h <= 200.0f);
        CHECK(r.x >= -r.w * 0.5f && r.x <= 64.0f - r.w * 0.5f);
        CHECK(r.y >= -r.h * 0.5f && r.y <= 32.0f - r.h * 0.5f);
        CHECK(r.a >= 253);
    }
}

void test_designer_strokes()
{
    using bench::DesignerStrokeKind;
    using bench::PathVerb;
    const auto connector = bench::path_template(DesignerStrokeKind::Connector);
    const auto elbow = bench::path_template(DesignerStrokeKind::Elbow);
    const auto bubble = bench::path_template(DesignerStrokeKind::SpeechBubble);
    CHECK(connector.command_count == 3);
    CHECK(elbow.command_count == 6);
    CHECK(bubble.command_count == 13);
    const std::vector<PathVerb> expected_connector = {
        PathVerb::Move, PathVerb::Cubic, PathVerb::Cubic};
    const std::vector<PathVerb> expected_elbow = {
        PathVerb::Move, PathVerb::Line, PathVerb::Cubic,
        PathVerb::Line, PathVerb::Cubic, PathVerb::Line};
    const std::vector<PathVerb> expected_bubble = {
        PathVerb::Move, PathVerb::Line, PathVerb::Cubic, PathVerb::Line,
        PathVerb::Cubic, PathVerb::Line, PathVerb::Line, PathVerb::Line,
        PathVerb::Line, PathVerb::Cubic, PathVerb::Line, PathVerb::Cubic,
        PathVerb::Close};
    const bench::NormalizedPathTemplate paths[] = {connector, elbow, bubble};
    const std::vector<PathVerb> expected_verbs[] = {
        expected_connector, expected_elbow, expected_bubble};
    for (size_t i = 0; i < 3; ++i) {
        const auto& path = paths[i];
        CHECK(path.commands != nullptr);
        if (!path.commands)
            continue;
        for (size_t j = 0; j < path.command_count; ++j) {
            const auto& command = path.commands[j];
            if (j < expected_verbs[i].size())
                CHECK(command.verb == expected_verbs[i][j]);
            CHECK(command.x1 >= 0.0f && command.x1 <= 1.0f);
            CHECK(command.y1 >= 0.0f && command.y1 <= 1.0f);
            if (command.verb == PathVerb::Cubic) {
                CHECK(command.x2 >= 0.0f && command.x2 <= 1.0f);
                CHECK(command.y2 >= 0.0f && command.y2 <= 1.0f);
                CHECK(command.x3 >= 0.0f && command.x3 <= 1.0f);
                CHECK(command.y3 >= 0.0f && command.y3 <= 1.0f);
            }
        }
    }

    // These fixed designer-strokes-v1 templates define the comparable workload.
    // Preserve the connector's curves and characteristic corners/bubble tail,
    // since bounds and verb counts alone also accept materially different paths.
    const auto check_point = [](float x, float y, float expected_x,
                                float expected_y) {
        CHECK_NEAR(x, expected_x, 0.000001);
        CHECK_NEAR(y, expected_y, 0.000001);
    };
    if (connector.commands && connector.command_count >= 3) {
        const auto& first = connector.commands[1];
        const auto& second = connector.commands[2];
        check_point(connector.commands[0].x1, connector.commands[0].y1, 0.08f,
                    0.80f);
        check_point(first.x1, first.y1, 0.28f, 0.80f);
        check_point(first.x2, first.y2, 0.32f, 0.68f);
        check_point(first.x3, first.y3, 0.50f, 0.50f);
        check_point(second.x1, second.y1, 0.68f, 0.32f);
        check_point(second.x2, second.y2, 0.72f, 0.20f);
        check_point(second.x3, second.y3, 0.92f, 0.20f);
    }
    if (elbow.commands && elbow.command_count >= 6) {
        const auto& corner = elbow.commands[2];
        check_point(elbow.commands[1].x1, elbow.commands[1].y1, 0.38f, 0.20f);
        check_point(corner.x1, corner.y1, 0.446274f, 0.20f);
        check_point(corner.x2, corner.y2, 0.50f, 0.253726f);
        check_point(corner.x3, corner.y3, 0.50f, 0.32f);
        check_point(elbow.commands[4].x3, elbow.commands[4].y3, 0.62f, 0.80f);
        check_point(elbow.commands[5].x1, elbow.commands[5].y1, 0.92f, 0.80f);
    }
    if (bubble.commands && bubble.command_count >= 8) {
        const auto& corner = bubble.commands[2];
        check_point(corner.x1, corner.y1, 0.88f, 0.14f);
        check_point(corner.x2, corner.y2, 0.94f, 0.24f);
        check_point(corner.x3, corner.y3, 0.94f, 0.37f);
        check_point(bubble.commands[5].x1, bubble.commands[5].y1, 0.48f, 0.82f);
        check_point(bubble.commands[6].x1, bubble.commands[6].y1, 0.32f, 0.96f);
        check_point(bubble.commands[7].x1, bubble.commands[7].y1, 0.35f, 0.82f);
    }

    bench::DesignerStrokeGenConfig config;
    config.canvas_width = 64;
    config.canvas_height = 32;
    config.shape_count = 128;
    config.min_size = 8.0f;
    config.max_size = 24.0f;
    config.min_stroke_width = 2.0f;
    config.max_stroke_width = 6.0f;
    const auto shapes = bench::generate_designer_strokes(42, config);
    const auto repeat = bench::generate_designer_strokes(42, config);
    CHECK(shapes.size() == 128);
    CHECK(repeat.size() == shapes.size());
    const DesignerStrokeKind cycle[] = {DesignerStrokeKind::Connector,
                                        DesignerStrokeKind::Elbow,
                                        DesignerStrokeKind::SpeechBubble};
    if (!shapes.empty()) {
        const auto& s = shapes[0];
        CHECK_NEAR(s.x, 15.7962818, 0.00002);
        CHECK_NEAR(s.y, 19.3137684, 0.00002);
        CHECK_NEAR(s.w, 12.8715334, 0.00002);
        CHECK_NEAR(s.h, 22.3446121, 0.00002);
        CHECK_NEAR(s.stroke_width, 3.8056495, 0.000002);
        CHECK(s.r == 116 && s.g == 168 && s.b == 30);
    }
    for (size_t i = 0; i < shapes.size(); ++i) {
        const auto& s = shapes[i];
        CHECK(s.w >= 8.0f && s.w <= 24.0f);
        CHECK(s.h >= 8.0f && s.h <= 24.0f);
        CHECK(s.x >= -s.w * 0.5f && s.x <= 64.0f - s.w * 0.5f);
        CHECK(s.y >= -s.h * 0.5f && s.y <= 32.0f - s.h * 0.5f);
        CHECK(s.stroke_width >= 2.0f && s.stroke_width <= 6.0f);
        CHECK(s.kind == cycle[i % 3]);
        if (i < repeat.size()) {
            const auto& r = repeat[i];
            CHECK(s.x == r.x && s.y == r.y && s.w == r.w && s.h == r.h);
            CHECK(s.stroke_width == r.stroke_width && s.kind == r.kind);
            CHECK(s.r == r.r && s.g == r.g && s.b == r.b);
        }
    }
    config.shape_count = 0;
    CHECK(bench::generate_designer_strokes(42, config).empty());
    config.shape_count = 2;
    config.canvas_width = config.canvas_height = 0;
    config.min_size = config.max_size = 12.0f;
    config.min_stroke_width = config.max_stroke_width = 4.0f;
    for (const auto& s : bench::generate_designer_strokes(42, config)) {
        CHECK(s.w == 12.0f && s.h == 12.0f);
        CHECK(s.x == -6.0f && s.y == -6.0f);
        CHECK(s.stroke_width == 4.0f);
    }
    const bench::DesignerStrokeData positioned = {
        10.0f, -4.0f, 80.0f, 20.0f, 2.0f, 0, 0, 0,
        DesignerStrokeKind::Connector};
    CHECK(bench::path_x(positioned, 0.0f) == 10.0f);
    CHECK(bench::path_x(positioned, 0.25f) == 30.0f);
    CHECK(bench::path_x(positioned, 1.0f) == 90.0f);
    CHECK(bench::path_y(positioned, 0.0f) == -4.0f);
    CHECK(bench::path_y(positioned, 0.25f) == 1.0f);
    CHECK(bench::path_y(positioned, 1.0f) == 16.0f);
}

}  // namespace

int main()
{
    test_stats();
    test_rng();
    test_rectangles();
    test_circles();
    test_transforms();
    test_multi_image_layout();
    test_designer_strokes();
    return test_result();
}

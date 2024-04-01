#include "definitions.hpp"
#include "lobe.hpp"
#include "math.hpp"
#include "topography.hpp"
#include "xtensor/xbuilder.hpp"
#include <fmt/format.h>
#include <fmt/ostream.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_range_equals.hpp>

TEST_CASE( "bounding_box", "[bounding_box]" )
{
    Flowtastic::VectorX x_data      = xt::arange<double>( 0.5, 19.5, 1.0 );
    Flowtastic::VectorX y_data      = xt::arange<double>( 4.5, 10.5, 1.0 );
    Flowtastic::MatrixX height_data = xt::zeros<double>( { x_data.size(), y_data.size() } );

    auto topography = Flowtastic::Topography( height_data, x_data, y_data );

    Flowtastic::Vector2 point = { 11.4, 7.6 };

    int idx_point_x_expected  = 10;
    int idx_point_y_expected  = 3;
    double cell_size_expected = 1.0;

    auto point_indices = topography.locate_point( point );
    REQUIRE( idx_point_x_expected == point_indices[0] );
    REQUIRE( idx_point_y_expected == point_indices[1] );
    REQUIRE_THAT( topography.cell_size(), Catch::Matchers::WithinRel( cell_size_expected ) );

    fmt::print( "point.idx_x = {}, point.idx_y = {}\n", point_indices[0], point_indices[1] );

    // Note: this bounding box is chosen such that no clamping will occur
    auto bbox = topography.bounding_box( point, 2 );
    fmt::print( "bbox.idx_x_higher = {}\n", bbox.idx_x_higher );
    fmt::print( "bbox.idx_x_lower = {}\n", bbox.idx_x_lower );
    fmt::print( "bbox.idx_y_lower = {}\n", bbox.idx_y_lower );
    fmt::print( "bbox.idx_y_higher = {}\n", bbox.idx_y_higher );

    int idx_x_lower_expected  = idx_point_x_expected - 2;
    int idx_x_higher_expected = idx_point_x_expected + 2;
    int idx_y_lower_expected  = idx_point_y_expected - 2;
    int idx_y_higher_expected = idx_point_y_expected + 2;

    REQUIRE( bbox.idx_x_higher == idx_x_higher_expected );
    REQUIRE( bbox.idx_x_lower == idx_x_lower_expected );
    REQUIRE( bbox.idx_y_lower == idx_y_lower_expected );
    REQUIRE( bbox.idx_y_higher == idx_y_higher_expected );
}

TEST_CASE( "test_point_in_cell", "[point_in]" )
{
    Flowtastic::VectorX x_data      = xt::arange<double>( 0, 5, 1.0 );
    Flowtastic::VectorX y_data      = xt::arange<double>( 0, 5, 1.0 );
    Flowtastic::MatrixX height_data = xt::zeros<double>( { x_data.size(), y_data.size() } );

    auto topography = Flowtastic::Topography( height_data, x_data, y_data );

    // Note that the left and bottom border of the cell are included, while the right and top are not
    Flowtastic::Vector2 point_in  = { 0.99, 0.5 };
    Flowtastic::Vector2 point_out = { 1.00, 0.5 };
    REQUIRE( topography.point_in_cell( 0, 0, point_in ) );
    REQUIRE( !topography.point_in_cell( 0, 0, point_out ) );
    REQUIRE( topography.point_in_cell( 1, 0, point_out ) );
}

TEST_CASE( "line_intersect", "[line_intersect]" )
{
    Flowtastic::VectorX x_data      = xt::arange<double>( 0, 5, 1.5 );
    Flowtastic::VectorX y_data      = xt::arange<double>( 0, 5, 1.5 );
    Flowtastic::MatrixX height_data = xt::zeros<double>( { x_data.size(), y_data.size() } );

    auto topography = Flowtastic::Topography( height_data, x_data, y_data );

    // Note that the left and bottom border of the cell are included, while the right and top are not
    double slope_xy   = -1;
    double offset_in  = 1.0;
    double offset_out = 3.1;

    REQUIRE( topography.line_intersects_cell( 0, 0, slope_xy, offset_in ) );
    REQUIRE( !topography.line_intersects_cell( 0, 0, slope_xy, offset_out ) );
}

TEST_CASE( "line_segment_intersect", "[line_segment_intersect]" )
{
    Flowtastic::VectorX x_data      = xt::arange<double>( -2, 2.1, 4 );
    Flowtastic::VectorX y_data      = xt::arange<double>( -2, 2.1, 4 );
    Flowtastic::MatrixX height_data = xt::zeros<double>( { x_data.size(), y_data.size() } );

    auto topography = Flowtastic::Topography( height_data, x_data, y_data );

    // Note that the left and bottom border of the cell are included, while the right and top are not

    Flowtastic::Vector2 x1 = { -3, 0 };
    Flowtastic::Vector2 x2 = { -1.99, 0 };

    fmt::print("x2 = {}\n", fmt::streamed(x2)); 
    REQUIRE( topography.line_segment_intersects_cell( 0, 0, x1, x2 ) );

    x2 = { 0, 0 };
    fmt::print("x2 = {}\n", fmt::streamed(x2)); 
    REQUIRE( topography.line_segment_intersects_cell( 0, 0, x1, x2 ) );

    x2 = { 3, 0 };
    fmt::print("x2 = {}\n", fmt::streamed(x2)); 
    REQUIRE( topography.line_segment_intersects_cell( 0, 0, x1, x2 ) );

    x2 = { 0, -2 };
    fmt::print("x2 = {}\n", fmt::streamed(x2)); 
    REQUIRE( topography.line_segment_intersects_cell( 0, 0, x1, x2 ) );

    x2 = { 0, 2 };
    fmt::print("x2 = {}\n", fmt::streamed(x2)); 
    REQUIRE( topography.line_segment_intersects_cell( 0, 0, x1, x2 ) );

    // These shouldnt intersect the cell
    x2 = { -2.5, 0 };
    fmt::print("x2 = {}\n", fmt::streamed(x2)); 
    REQUIRE( !topography.line_segment_intersects_cell( 0, 0, x1, x2 ) );

    x2 = { -2.0, 2.01 };
    fmt::print("x2 = {}\n", fmt::streamed(x2)); 
    REQUIRE( !topography.line_segment_intersects_cell( 0, 0, x1, x2 ) );
}

    auto [intersection, bbox] = topography.compute_intersection( my_lobe );

    fmt::print( "bbox.idx_x_higher = {}\n", bbox.idx_x_higher );
    fmt::print( "bbox.idx_x_lower = {}\n", bbox.idx_x_lower );
    fmt::print( "bbox.idx_y_lower = {}\n", bbox.idx_y_lower );
    fmt::print( "bbox.idx_y_higher = {}\n", bbox.idx_y_higher );
    fmt::print( "intersection = {}\n", fmt::streamed( intersection( 0, 0 ) ) );
}
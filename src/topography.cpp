#include "topography.hpp"
#include "asc_file.hpp"
#include "definitions.hpp"
#include "xtensor/xbuilder.hpp"
#include <fmt/ranges.h>
#include <algorithm>
#include <stdexcept>
#include <vector>

namespace Flowy
{

AscFile Topography::to_asc_file( Topography::Output output )
{
    AscFile asc_file{};
    asc_file.lower_left_corner = { x_data[0], y_data[0] };
    asc_file.cell_size         = cell_size();

    if( output == Topography::Output::Height )
    {
        asc_file.height_data = height_data;
    }
    else
    {
        asc_file.height_data = hazard;
    }
    return asc_file;
}

bool Topography::is_point_near_boundary( const Vector2 & coordinates, double radius )
{
    int n = std::ceil( radius / cell_size() );

    const bool near_x_boundary
        = coordinates[0] < x_data[0] + n * cell_size() || coordinates[0] >= x_data.periodic( -1 ) - n * cell_size();
    const bool near_y_boundary
        = coordinates[1] < y_data[0] + n * cell_size() || coordinates[1] >= y_data.periodic( -1 ) - n * cell_size();
    return near_x_boundary || near_y_boundary;
}

std::array<int, 2> Topography::locate_point( const Vector2 & coordinates )
{
    const bool outside_x = coordinates[0] < x_data[0] || coordinates[0] >= x_data.periodic( -1 ) + cell_size();
    const bool outside_y = coordinates[1] < y_data[0] || coordinates[1] >= y_data.periodic( -1 ) + cell_size();

    if( outside_x || outside_y )
    {
        throw std::runtime_error( "Cannot locate point, because coordinates are outside of grid!" );
    }

    const int idx_x = int( ( coordinates[0] - x_data[0] ) / cell_size() );
    const int idx_y = int( ( coordinates[1] - y_data[0] ) / cell_size() );
    return { idx_x, idx_y };
}

Topography::BoundingBox Topography::bounding_box( const Vector2 & center, double extent_x, double extent_y )
{
    const auto [idx_x_lower, idx_y_lower]  = locate_point( center );
    const int number_of_cells_to_include_x = std::ceil( extent_x / cell_size() );
    const int number_of_cells_to_include_y = std::ceil( extent_y / cell_size() );

    Topography::BoundingBox res{};
    res.idx_x_lower  = std::clamp<int>( idx_x_lower - number_of_cells_to_include_x, 0, x_data.size() - 1 );
    res.idx_x_higher = std::clamp<int>( idx_x_lower + number_of_cells_to_include_x, 0, x_data.size() - 1 );
    res.idx_y_lower  = std::clamp<int>( idx_y_lower - number_of_cells_to_include_y, 0, y_data.size() - 1 );
    res.idx_y_higher = std::clamp<int>( idx_y_lower + number_of_cells_to_include_y, 0, y_data.size() - 1 );

    return res;
}

LobeCells Topography::get_cells_intersecting_lobe( const Lobe & lobe, std::optional<int> idx_cache )
{
    // Can we use the cache?
    bool use_cache = idx_cache.has_value() && ( intersection_cache.size() > idx_cache.value() );

    if( use_cache )
    {
        std::optional<LobeCells> cached_lobe_cells = intersection_cache[idx_cache.value()];

        // Does the cache already contain a value?
        if( cached_lobe_cells.has_value() ) // If yes, we return it
        {
            return cached_lobe_cells.value();
        }
    }

    LobeCells res{};
    LobeCells::cellvecT cells_intersecting{};
    LobeCells::cellvecT cells_enclosed{};

    auto extent_xy = lobe.extent_xy();

    // push_back cells with x index in the interval [idx_start, idx_stop]
    auto push_back_cells = [&]( LobeCells::cellvecT & cells, int idx_start, int idx_stop, int idx_y )
    {
        for( int idx_x = idx_start; idx_x <= idx_stop; idx_x++ )
        {
            cells.push_back( { idx_x, idx_y } );
        }
    };

    // The minimum and the maximum y index of the bounding box
    int idx_y_min = ( lobe.center[1] - extent_xy[1] - y_data[0] ) / cell_size();
    int idx_y_max = ( lobe.center[1] + extent_xy[1] - y_data[0] ) / cell_size();

    // We scan the bounding box of the ellipse in rows
    const int n_rows = idx_y_max - idx_y_min + 1;

    // At each row we record the x index of the cell where the lobe intersects the row
    // We use -1 to signal no intersection
    // For each row, there is one intersection on the left and one on the right
    auto idx_x_left  = std::vector<int>( n_rows + 1, -1 );
    auto idx_x_right = std::vector<int>( n_rows + 1, -1 );

    // Since coordinates start at the lower left corner, we only iterate from between
    // idx_y_min + 1 and idx_y_max, since we know that the bottom of the first row
    // and the top of the last row have no intersections
    for( int idx_y = idx_y_min + 1; idx_y <= idx_y_max + 1; idx_y++ )
    {
        const int idx_row = idx_y - idx_y_min;

        // We comupute the y value at which to check for intersections
        // We do not use y_data[idx_y] since idx_y_max + 1 might be outside the boundaries of the y_data array
        const double y = y_data[idx_y];

        // These two point define a horizontal line, at the bottom of the current row
        const Vector2 x1  = { lobe.center[0] - extent_xy[0], y };
        const Vector2 x2  = { lobe.center[0] + extent_xy[0], y };
        const auto points = lobe.line_segment_intersects( x1, x2 );

        // only if intersections are found, we can unpack them into x indices
        if( points.has_value() )
        {
            const auto p1        = points.value()[0];
            const auto p2        = points.value()[1];
            idx_x_left[idx_row]  = ( p1[0] - x_data[0] ) / cell_size();
            idx_x_right[idx_row] = ( p2[0] - x_data[0] ) / cell_size();
        }

        const int & idx_left_cur  = idx_x_left[idx_row];
        const int & idx_right_cur = idx_x_right[idx_row];

        // The bottom of the next row, is the top of the previous one
        // Therefore, we now know the intersections at the bottom and at the top of the *previous* row

        // Since we start from idx_y_min + 1, idx_row starts at 1 too
        const double idx_left_prev  = idx_x_left[idx_row - 1];
        const double idx_right_prev = idx_x_right[idx_row - 1];

        // We treat the first and the last row separately, since here, there are no intersections
        // with the previous row (in case of the first) or the current row (in case of the last row)
        if( idx_y == idx_y_min + 1 )
        {
            push_back_cells( cells_intersecting, idx_left_cur, idx_right_cur, idx_y_min );
        }
        else if( idx_y == idx_y_max + 1 )
        {
            push_back_cells( cells_intersecting, idx_left_prev, idx_right_prev, idx_y_max );
        }
        else
        {
            const int start_left = std::min<int>( idx_left_prev, idx_left_cur );
            const int stop_left  = std::max<int>( idx_left_prev, idx_left_cur );
            push_back_cells( cells_intersecting, start_left, stop_left, idx_y - 1 );

            const int start_right = std::min<int>( idx_right_prev, idx_right_cur );
            const int stop_right  = std::max<int>( idx_right_prev, idx_right_cur );
            push_back_cells( cells_intersecting, start_right, stop_right, idx_y - 1 );

            push_back_cells( cells_enclosed, stop_left + 1, start_right - 1, idx_y - 1 );
        }
    }

    res.cells_intersecting = std::move( cells_intersecting );
    res.cells_enclosed     = std::move( cells_enclosed );

    // If the cache is used, we copy the intersection data there
    if( use_cache )
    {
        intersection_cache[idx_cache.value()] = res;
    }

    return res;
}

std::vector<std::pair<std::array<int, 2>, double>>
Topography::compute_intersection( const Lobe & lobe, std::optional<int> idx_cache, int N )
{
    auto lobe_cells = get_cells_intersecting_lobe( lobe, idx_cache );

    std::vector<std::pair<std::array<int, 2>, double>> res{};
    res.reserve( lobe_cells.cells_intersecting.size() + lobe_cells.cells_enclosed.size() );

    // All enclosed cells are fully covered
    for( const auto & [idx_x, idx_y] : lobe_cells.cells_enclosed )
    {
        res.push_back( { { idx_x, idx_y }, 1.0 } );
    }

    const double cell_size = this->cell_size();
    const double cell_area = cell_size * cell_size;
    const double step      = cell_size / N;

    // The intersecting cells get rasterized into columns
    for( const auto & [idx_x, idx_y] : lobe_cells.cells_intersecting )
    {
        const double y_min = y_data[idx_y];
        const double y_max = y_data[idx_y] + cell_size;

        double area = 0;
        for( int ix = 0; ix < N; ix++ )
        {
            const double x = x_data[idx_x] + step * ix;

            // For each column we check if the endpoints are in our outside of the lobe
            const bool y_min_in = lobe.is_point_in_lobe( { x, y_min } );
            const bool y_max_in = lobe.is_point_in_lobe( { x, y_max } );

            // If both endpoints are inside the lobe, the entire column is inside the lobe
            if( y_min_in && y_max_in )
            {
                area += cell_size;
                continue;
            }

            // If both endpoints are outside the lobe, the entire column is outside the lobe
            if( !( y_min_in || y_max_in ) )
            {
                continue;
            }

            // Now we know that one endpoint is inside the lobe and one endpoint is outside of it

            // {x,y_lo} should be inside the lobe
            double y_lo        = y_min_in ? y_min : y_max;
            const double y_end = y_lo;

            // {x,y_hi} should be outside the lobe
            double y_hi = !y_min_in ? y_min : y_max;
            double y_cur{};

            // Now we try to find the height at wich the ellipse passed the columns
            // with four iterations of bisection search
            for( int it = 0; it < 4; it++ )
            {
                y_cur = 0.5 * ( y_lo + y_hi );

                // If y_cur is inside the lobe, we make y_cur y_lo
                const bool y_cur_in = lobe.is_point_in_lobe( { x, y_cur } );
                y_lo                = y_cur_in ? y_cur : y_lo;
                y_hi                = !y_cur_in ? y_cur : y_hi;
            }
            area += std::abs( y_cur - y_end );
        }
        const double fraction = area * step / cell_area;
        res.push_back( { { idx_x, idx_y }, fraction } );
    }

    return res;
}

void Topography::compute_hazard_flow( const std::vector<Lobe> & lobes, MatrixX & flow_hazard )
{
    std::fill( flow_hazard.begin(), flow_hazard.end(), 0 );

    // This computes the hazard for *one* flow
    // For one flow, the hazard of a cell is the maximum of lobe.n_descendant over all lobes touching it
    for( size_t idx = 0; idx < lobes.size(); idx++ )
    {
        const auto & lobe = lobes[idx];
        auto lobe_cells   = get_cells_intersecting_lobe( lobe, idx );

        for( const auto & [idx_x, idx_y] : lobe_cells.cells_enclosed )
        {
            flow_hazard( idx_x, idx_y ) = std::max<int>( lobe.n_descendents, flow_hazard( idx_x, idx_y ) );
        }

        for( const auto & [idx_x, idx_y] : lobe_cells.cells_intersecting )
        {
            flow_hazard( idx_x, idx_y ) = std::max<int>( lobe.n_descendents, flow_hazard( idx_x, idx_y ) );
        }
    }
}

std::pair<double, Vector2> Topography::height_and_slope( const Vector2 & coordinates )
{
    const auto [idx_x, idx_y] = locate_point( coordinates );
    const Vector2 cell_center = { x_data[idx_x] + 0.5 * cell_size(), y_data[idx_y] + 0.5 * cell_size() };

    int idx_x_lower{}, idx_x_higher{};
    int idx_y_lower{}, idx_y_higher{};

    if( coordinates[0] > cell_center[0] )
    {
        idx_x_lower  = idx_x;
        idx_x_higher = std::min<int>( idx_x + 1, x_data.size() - 1 );
    }
    else
    {
        idx_x_lower  = std::max<int>( idx_x - 1, 0 );
        idx_x_higher = idx_x;
    }

    if( coordinates[1] > cell_center[1] )
    {
        idx_y_lower  = idx_y;
        idx_y_higher = std::min<int>( idx_y + 1, y_data.size() - 1 );
    }
    else
    {
        idx_y_lower  = std::max<int>( idx_y - 1, 0 );
        idx_y_higher = idx_y;
    }

    const Vector2 cell_center_lower_left
        = { x_data[idx_x_lower] + 0.5 * cell_size(), y_data[idx_y_lower] + 0.5 * cell_size() };

    // Which triangle are we in?

    const Vector2 diff = coordinates - cell_center_lower_left;
    const double dist2 = diff[0] * diff[0] + diff[1] * diff[1]; // square of the distance to the lower left corner

    double c{}, alpha{}, beta{};

    if( dist2 < 0.5 * cell_size() * cell_size() ) // we are in the lower left triangel
    {
        c     = height_data( idx_x_lower, idx_y_lower );
        alpha = ( height_data( idx_x_higher, idx_y_lower ) - height_data( idx_x_lower, idx_y_lower ) )
                / cell_size(); // derivative in x direction
        beta = ( height_data( idx_x_lower, idx_y_higher ) - height_data( idx_x_lower, idx_y_lower ) )
               / cell_size(); // derivative in y
    }
    else // we are in the upper right triangle
    {
        c     = height_data( idx_x_higher, idx_y_higher );
        alpha = ( height_data( idx_x_higher, idx_y_higher ) - height_data( idx_x_lower, idx_y_higher ) )
                / cell_size(); // derivative in x direction
        beta = ( height_data( idx_x_higher, idx_y_higher ) - height_data( idx_x_higher, idx_y_lower ) )
               / cell_size(); // derivative in y
    }

    double height = c + alpha * coordinates[0] + beta * coordinates[1];

    // height = height_data(idx_x, idx_y);

    const Vector2 slope = { alpha, beta };

    // const double Z00 = height_data( idx_x_lower, idx_y_lower );
    // const double Z10 = height_data( idx_x_higher, idx_y_lower );
    // const double Z01 = height_data( idx_x_lower, idx_y_higher );
    // const double Z11 = height_data( idx_x_higher, idx_y_higher );

    // const double alpha = Z10 - Z00;
    // const double beta  = Z01 - Z00;
    // const double gamma = Z11 + Z00 - Z10 - Z01;

    // const Vector2 xp = ( coordinates - cell_center_lower_left ) / cell_size();

    // const double height = Z00 + alpha * xp[0] + beta * xp[1] + gamma * xp[0] * xp[1];
    // const Vector2 slope = { alpha + gamma * xp[1], beta + gamma * xp[0] };

    return { height, -slope };
}

void Topography::add_lobe( const Lobe & lobe, std::optional<int> idx_cache )
{
    // In this function we simply add the thickness of the lobe to the topography
    // First, we find the intersected cells and the covered fractions
    std::vector<std::pair<std::array<int, 2>, double>> intersection_data = compute_intersection( lobe, idx_cache );

    // Then we add the tickness according to the fractions
    for( auto const & [indices, fraction] : intersection_data )
    {
        height_data( indices[0], indices[1] ) += fraction * lobe.thickness;
    }
}

Vector2 Topography::find_preliminary_budding_point( const Lobe & lobe, int npoints )
{
    // First, we rasterize the perimeter of the ellipse
    std::vector<Vector2> perimeter = lobe.rasterize_perimeter( npoints );

    // Then, we find the point of minimal elevation amongst the rasterized points on the perimeter
    auto min_elevation_point_it = std::min_element(
        perimeter.begin(), perimeter.end(), [&]( const Vector2 & p1, const Vector2 & p2 )
        { return height_and_slope( p1 ).first < height_and_slope( p2 ).first; } );

    return *min_elevation_point_it;
}

void Topography::reset_intersection_cache( int N )
{
    intersection_cache = std::vector<std::optional<LobeCells>>( N, std::nullopt );
}

} // namespace Flowy
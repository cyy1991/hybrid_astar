#ifndef INTERNAL_GRID_MAP_HPP
#define INTERNAL_GRID_MAP_HPP

#include "GVDLau.hpp"
#include "GridMapCell.hpp"
#include "../Entities/State2D.hpp"
#include "../Entities/Circle.hpp"

namespace astar {

class InternalGridMap {

    private:

        // PRIVATE ATTRIBUTES

        // the grid map width
        unsigned int width, width_2;

        // the grid map height
        unsigned int height, height_2;

        // the grid map size
        unsigned int size;

        // the grid map resolution
        double resolution, inverse_resolution, diagonal_resolution;

        // the grid map origin
        astar::Vector2D<double> origin;

        // the current grid map
        astar::GridMap grid_map;

        // the Voronoi field  map
        astar::GVDLau voronoi;

        // PRIVATE METHODS

    public:

        // PUBLIC ATTRIBUTES

        // PUBLIC METHODS

        // basic constructor
        InternalGridMap();

        // basic destructor
        ~InternalGridMap();

        // initialize the grid map given the map dimensions
        void InitializeGridMap(unsigned int w, unsigned int h, double res, const astar::Vector2D<double> &_origin);

        // verify if a given pose is a valid one
        bool isSafePlace(const std::vector<astar::Circle> &body, double safety_factor);

        // occupy a given cell
        void inline OccupyCell(int row, int col);

        // clear a given cell
        void inline ClearCell(int row, int col);

        // update the internal grid map
        void UpdateGridMap();

        // return a cell given a pose
        GridMapCellPtr PoseToCell(const astar::Pose2D&);

};

// just another helper
typedef InternalGridMap* InternalGridMapPtr;
typedef InternalGridMap& InternalGridMapRef;

// i is the row and j is the column ;-)
#define GRID_MAP_INDEX(j, i) ((j)*width + i)

}

#endif
#include "StanleyController.hpp"

#include <limits>
#include <array>

#include <opencv2/opencv.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>

using namespace astar;

// basic constructor
StanleyController::StanleyController(const InternalGridMapRef grid_map, const VehicleModel &vehicle) :
    grid(grid_map),
    vehicle_model(vehicle), cs(CSStandby), next_waypoint(0), prev_waypoint(0),
    dt(0.1), reverse_mode(false), front_axle(), fake_front_axle(), closest_point(),
    left(), right(), raw_path(), forward_path(), reverse_path(), consolidated_path(false),
    raw_path_size(0), raw_path_last_index(0), stopping(), prev_wheel_angle_error(0),
    vpasterror(0.0), last_cusp(0)
{
}

// update the path around stopping points
void StanleyController::UpdateLowSpeedRegions() {

    // get the stopping points vector size
    unsigned int s_size = stopping.size();

    double acceleration_constraint;
    int prev, next, current_stop;

    // some ref helper
    astar::State2D &prev_state(car), &next_state(car);

    for (unsigned int i = 0; i < s_size; i++) {

        // get the path index
        prev = next = current_stop = stopping[i];

        // go to the left
        prev -= 1;

        if (0 <= prev) {

            // the speed should be low around the stopping point
            raw_path[prev].v = forward_path[prev].v = reverse_path[prev].v = 0.2;

            // it's a coming to stop case
            raw_path[prev].coming_to_stop = forward_path[prev].coming_to_stop = reverse_path[prev].coming_to_stop = true;

            // update the next index
            next = prev;

            // update the prev index
            prev -= 1;

            while (0 <= prev) {

                // get the desired references
                prev_state = raw_path[prev];
                next_state = raw_path[next];

                acceleration_constraint = vehicle_model.GetDecelerationConstraint(next_state.v, prev_state.position.Distance(next_state.position), prev_state.gear);

                // update the current speed
                if (acceleration_constraint < prev_state.v) {

                    raw_path[prev].v = forward_path[prev].v = reverse_path[prev].v = acceleration_constraint;

                } else {

                    break;

                }

                // update the indexes
                next = prev;
                prev -= 1;

            }

        }

        // reset the indexes
        prev = next = current_stop;

        // go to the right
        next += 1;
        if (raw_path_size > next) {

            // the speed should be low around the stopping point
            raw_path[next].v = forward_path[next].v = reverse_path[next].v = 0.2;

            // update the prev index
            prev = next;

            // update the next index
            next += 1;

            while(raw_path_size > next) {

                // get the desired references
               prev_state = raw_path[prev];
               next_state = raw_path[next];

               acceleration_constraint = vehicle_model.GetAccelerationConstraint(next_state.v, prev_state.position.Distance(next_state.position), prev_state.gear);

               // update the current speed
               if (acceleration_constraint < next_state.v) {

                   raw_path[next].v = forward_path[next].v = reverse_path[next].v = acceleration_constraint;

               } else {

                   break;

               }

                // update the indexes
                prev = next;
                next += 1;

            }

        }

    }

}

// the first and the last states remains the same
bool StanleyController::ConsolidateStateList(StateArrayPtr input_path) {

    // syntactic sugar
    std::vector<State2D> &input(input_path->states);

    // clear the internal paths
    raw_path.clear();
    forward_path.clear();
    reverse_path.clear();

    // clear the stopping list
    stopping.clear();

    // get the limit iterators
    std::vector<State2D>::iterator end = input.end();
    std::vector<State2D>::iterator last = --input.end();

    // get the list iterators
    std::vector<State2D>::iterator prev = input.begin();
    std::vector<State2D>::iterator current = prev;
    std::advance(current, 1);

    std::vector<State2D>::iterator next = current;
    std::advance(next, 1);

    // save the first state to the internal state lists
    raw_path.push_back(*prev);
    forward_path.push_back(vehicle_model.GetFrontAxleState(*prev));
    reverse_path.push_back(vehicle_model.GetFakeFrontAxleState(*prev));

    // register the stopping points index
    unsigned int index = 0;

    // is the car stopped?
    if (0.0 == prev->v) {

       // save to the stopping points list
        stopping.push_back(index);

        // set the car controller state
        cs = CSStopped;

    } else {

        if (ForwardGear == prev->gear) {

            // set the car controller state
            cs = CSForwardDrive;

        } else {

            // set the car controller state
            cs = CSReverseDrive;

        }

    }

    // update the index
    index += 1;

    // iterate the entire list but the second last element
    while (next != last) {

        // the common case
        if (current->gear == prev->gear) {

            if (ForwardGear == current->gear) {

                // get the desired orientation at this point
                current->orientation = vehicle_model.GetForwardOrientation(*prev, *current, *next);

                // get the max speed
                current->v = vehicle_model.GetForwardSpeed(*prev, *current, *next);


            } else {

                // get the desired orientation at this point
                current->orientation = vehicle_model.GetBackwardOrientation(*prev, *current, *next);

                // get the max speed
                current->v = vehicle_model.GetBackwardSpeed(*prev, *current, *next);

            }


        } else {

            // the speed should be zero around any stopping point
            current->v = 0.0;

            // save to the stopping points list
            stopping.push_back(index);

        }

        // append to the current raw path
        raw_path.push_back(*current);

        // append to the current forward path
        forward_path.push_back(vehicle_model.GetFrontAxleState(*current));

        // append to the current internal path
        reverse_path.push_back(vehicle_model.GetFakeFrontAxleState(*current));

        // update the index
        index += 1;

        // update the iterators
        prev = current;
        current = next;
        ++next;

    }

    // the last point has the goal speed
    // we don't need to change that speed
    // append to the current raw path
    raw_path.push_back(*last);

    // append to the current forward path
    forward_path.push_back(vehicle_model.GetFrontAxleState(*last));

    // append to the current reverse path
    reverse_path.push_back(vehicle_model.GetFakeFrontAxleState(*last));

    // verify the parking mode
    // save to the stopping points list
    stopping.push_back(index);

    // update the reference sizes
    // get the input path size and the last index
    raw_path_size = raw_path.size();
    raw_path_last_index = raw_path_size - 1;

    // get the required speed around curves and stopping points
    // TODO -> we need to get slower speed next to obstacles
    UpdateLowSpeedRegions();

    // set the prev and next waypoints
    prev_waypoint = 0;
    next_waypoint = 1;
    last_cusp = 0;

    return true;

}

// how far the car has moved between two points in the path
double StanleyController::HowFarAlong(const Pose2D &current, const Pose2D &prev, const Pose2D &next) {

    Vector2D<double> r = current.position - prev.position;
    Vector2D<double> d = next.position - prev.position;

    return ((r.x * d.x + r.y * d.y)/(d.x * d.x + d.y * d.y));

}

// find the previous and next index in the path
void StanleyController::Localize(const astar::State2D &s, unsigned int &prev_index, unsigned int &next_index) {

    // Find closest path point to back axle
    double inf = std::numeric_limits<double>::max();
    double bestd = inf;

    int start = std::max<int>(last_cusp, std::max<int>(0, next_waypoint - 2));
    int end = std::min<int>(raw_path_last_index, next_waypoint + 2);

    int besti = start;

    for (int i = start + 1; i < end; i++) {

        if (raw_path[i-1].coming_to_stop)
            break;

        float d = s.position.Distance(raw_path[i].position);
        if (d < bestd) {

            bestd = d;
            besti = i;

        }

    }

    next_index = besti;

    if (raw_path_size <= besti + 1) {

        prev_index = besti - 1;

    } else if (0 > besti - 1) {

        prev_index = next_index;
        next_index = besti + 1;

    } else if (besti == last_cusp) {

        next_index = besti + 1;
        prev_index = besti;

    } else {

        Vector2D<double> prevmaybe(raw_path[besti - 1].position);
        Vector2D<double> nextmaybe(raw_path[besti + 1].position);

        if (s.position.Distance2(prevmaybe) < s.position.Distance2(nextmaybe)) {

            prev_index = besti - 1;

        } else {

            // TODO
            prev_index = next_index;
            next_index = besti + 1;

        }

    }

    if (0 < prev_index && (raw_path[prev_index - 1].coming_to_stop && prev_index != prev_waypoint)){

        prev_index--;
        next_index--;

    }

}

// find the closest point next to the front axle, fake or not
Pose2D StanleyController::FindClosestPoint(const astar::State2D &s, const astar::State2D &prev, const astar::State2D &next) {

    Pose2D pose;

    if (next.position.x == prev.position.x) {

        // Avoid division by zero
        pose.position.x = next.position.x;
        pose.position.y = s.position.y;

    } else {

        double m = (next.position.y - prev.position.y) / (next.position.x - prev.position.x);
        double m2 = m * m;
        double b = next.position.y - m * next.position.x;

        pose.position.x = (m * s.position.y + s.position.x - m * b) / (m2 + 1);
        pose.position.y = (m2 * s.position.y + m * s.position.x + b) / (m2 + 1);

    }

    return pose;

}

// the correct action at some stopping point
State2D StanleyController::Stopped(const State2D &s) {

    double phi, phi_error, d_phi_error, ddphi;
    double desired_heading;

    ControllerState next_controller_state;

    State2D state(s);

    bool reverse_mode = BackwardGear == raw_path[prev_waypoint].gear;

    // syntactic sugar
    std::vector<State2D> &the_path = reverse_mode ? reverse_path : forward_path;

    State2D &next(the_path[next_waypoint]);
    State2D &prev(the_path[prev_waypoint]);

    if (raw_path_last_index == next_waypoint && raw_path_last_index == prev_waypoint) {

        phi = s.phi;

        // mission complete
        next_controller_state = CSComplete;

    } else {

        Vector2D<double> prev_point(prev.position);
        Vector2D<double> next_point(next.position);

        desired_heading = std::atan2(next_point.y - prev_point.y, next_point.x - prev_point.x);

        // set the next state as forward
        next_controller_state = CSForwardDrive;

    }

    phi_error = -mrpt::math::wrapToPi(s.orientation - desired_heading) - s.phi;

    // clamp th
    // reusing the phi variable
    phi = phi_error * 8.0 + d_phi_error * 0.1;

    // clamp to -1:1
    phi = std::max(-1.0, std::min(1.0, phi));

    // get the steering motor speed
    phi = s.phi + 0.025 * (phi * vehicle_model.max_phi_velocity -s.phi/ vehicle_model.max_wheel_deflection * s.v * 0.01);

    if (std::fabs(phi) > vehicle_model.max_wheel_deflection) {

        phi = (0 < phi) ? vehicle_model.max_wheel_deflection : -vehicle_model.max_wheel_deflection;

    }

    if (0.002 > std::fabs(phi_error) || 0.1 > std::fabs(mrpt::math::wrapToPi<double>(std::fabs(state.phi) - vehicle_model.max_wheel_deflection))) {

        // start to move
        cs = next_controller_state;

        // what is desired speed?
        prev.v = next.v;

        // reset the prev wheel angle error
        prev_wheel_angle_error = 0.0;

    }

    state.v = 0.0;
    state.phi = phi;
    state.t = dt;

    return state;

}

// the main Stanley method
State2D StanleyController::ForwardDrive(const State2D &s) {

    unsigned int prev_index, next_index;

    // find the previous and next index in the path
    Localize(s, prev_index, next_index);

    // update the way points
    prev_waypoint = prev_index;
    next_waypoint = next_index;

    // copy the input state
    State2D state(s);

    // set the reverse mode flag
    reverse_mode = (BackwardGear == raw_path[prev_index].gear);

    std::vector<State2D> &the_path = reverse_mode ? reverse_path : forward_path;

    // set the coming to stop flag
    bool coming_to_stop = 0.0 == the_path[next_index].v;

    // get the previous and next states
    State2D &prev(the_path[prev_index]);
    State2D &next(the_path[next_index]);

    fake_front_axle = vehicle_model.GetFakeFrontAxleState(s);
    front_axle = reverse_mode ? fake_front_axle : vehicle_model.GetFrontAxleState(s);

    closest_point = FindClosestPoint(front_axle, prev, next);

    // verificar se está funcionando
    double how_far = std::min(std::max(0.0 , HowFarAlong(s, prev, next)), 1.0);

    Vector2D<double> prev_point(prev.position);
    Vector2D<double> next_point(next.position);

    Vector2D<double> heading = (0 < prev_index) ? next_point - the_path[prev_index - 1].position : next_point - prev_point;
    double desired_heading = std::atan2(heading.y, heading.x);

    double next_heading;

    if (coming_to_stop) {

        next_heading = next.orientation + (reverse_mode ? M_PI : 0);

    } else {

        Vector2D<double> nhv = the_path[next_index + 1].position - prev.position;
        next_heading = std::atan2(nhv.y, nhv.x);

    }

    // update the desired heading
    desired_heading += mrpt::math::wrapToPi<double>(next_heading - desired_heading)*how_far;

    // Is the path to our left or right?
    Vector2D<double> norm(next_point.y - prev_point.y, next_point.x - prev_point.x);
    norm.Normalize();

    left = norm;
    right = norm;

    left.RotateZ(M_PI_2);
    left.Scale(2.0);
    left.Add(closest_point.position);

    right.RotateZ(-M_PI_2);
    right.Scale(2.0);
    right.Add(closest_point.position);

    double direction = 1;

    if (left.Distance2(front_axle.position) < right.Distance2(front_axle.position)) {

        direction = -1;

    }

    double k = 1.5;
    double dist = front_axle.position.Distance(closest_point.position);
    double d_theta = mrpt::math::wrapToPi<double>(s.orientation - desired_heading + (reverse_mode ? M_PI : 0));

    if (reverse_mode) {

        d_theta = -d_theta;
        direction = -direction;
    }

    double phi;
    double inverse_speed = (4.5 < s.v) ? 1.0/s.v: 1.0;

    if (next.coming_to_stop) {

        phi = std::atan(4.0 * k * dist * direction * inverse_speed);

    } else {

        phi = mrpt::math::wrapToPi<double>(-d_theta + std::atan(k * dist * direction * inverse_speed));

    }

    double phi_error = phi - s.phi;
    double d_phi_error = (phi_error - prev_wheel_angle_error) / 0.025;
    prev_wheel_angle_error = phi_error;

    // reusing the phi variable
    phi = phi_error * 2.0 + d_phi_error * 0.1;

    // clamp to -1:1
    phi = std::max(-1.0, std::min(1.0, phi));

    // get the steering motor speed
    double steer = s.phi + 0.025 * (phi * vehicle_model.max_phi_velocity -s.phi/ vehicle_model.max_wheel_deflection * s.v * 0.01);

    if (std::fabs(steer) > vehicle_model.max_wheel_deflection) {

        steer = (0 < steer) ? vehicle_model.max_wheel_deflection : -vehicle_model.max_wheel_deflection;

    }

    // update the command
    steer = ((double) ((int) (steer * 1000))) * 0.001;

    // velocity
    double vp = 0.5;
    double vi = 0.00005;
    double dv = 0;

    // s.v + lerp(v0, v1, how_far)
    double verror = s.v - ((1 - how_far)*prev.v + how_far*next.v);

    vpasterror = verror * 0.025;
    double v_total_error = vp * verror + vi*vpasterror;

    dv = -v_total_error;

    if (reverse_mode)
        dv = -dv;

    std::cout << "waypoints: " << prev_waypoint << ", " << next_waypoint << " and phi: " << steer << " and cross: " << dist << "\n";

    if (coming_to_stop && 0.95 <= how_far) {

        last_cusp = next_index;

        if (next_index < raw_path_last_index) {

            next_waypoint += 1;
            prev_waypoint += 1;

        } else {

            next_waypoint = prev_waypoint = raw_path_last_index;

        }

        // set the stopped controller state
        cs = CSStopped;

        // update the command to stop next point
        steer = ((double) ((int) (steer * 1000))) * 0.001;

        state.v = 0.0;
        state.phi = steer;
        state.t = dt;

        return state;

    }

    dv = ((double) ((int) (dv * 1000))) * 0.001;

    state.v += dv;
    state.phi = steer;
    state.t = dt;

    return state;

}

// travel along the path - forward
State2D StanleyController::ReverseDrive(const State2D &s) {

    return ForwardDrive(s);

}

// path following simulation
StateArrayPtr StanleyController::FollowPathSimulation(astar::StateArrayPtr path) {

    return path;

}

// follow a given path
StateArrayPtr StanleyController::FollowPath(const State2D &start) {

    // resulting command list
    StateArrayPtr command_array = new StateArray();

    // syntactic sugar
    std::vector<State2D> &commands(command_array->states);

    // set the car pose
    car = start;

    while (CSComplete != cs) {

        switch (cs) {

            case CSForwardDrive:

                // get the next control
                car = ForwardDrive(car);

                // save to the command list
                commands.push_back(car);

                break;

            case CSReverseDrive:

                // get the next control
                car = ReverseDrive(car);

                // save to the command list
                commands.push_back(car);

                break;

            case CSStopped:

                // get the next control and pose
                car = Stopped(car);

                // save to the command list
                commands.push_back(car);

                break;

            case CSStandby:

                cs = CSStopped;

                break;

            default:
                break;

        }

        // update the car pose
        car = vehicle_model.NextState(car);

    }

    // reset the current state

    return command_array;

}

// get a Command list to follow a given path
StateArrayPtr StanleyController::BuildAndFollowPath(astar::StateArrayPtr input_path) {

    if (0 < input_path->states.size()) {

        consolidated_path = ConsolidateStateList(input_path);

        return FollowPathSimulation(input_path);

    }

    consolidated_path = false;

    return new StateArray();

}

// consolidate the input path  and build a new command list
StateArrayPtr StanleyController::RebuildCommandList(const astar::State2D &start, astar::StateArrayPtr path) {

    //
    consolidated_path = ConsolidateStateList(path);

    return GetCommandList(start);

}

// get the next command
StateArrayPtr StanleyController::GetCommandList(const astar::State2D &start) {

    car = start;
    astar::StateArrayPtr commands = new StateArray();
    std::vector<astar::State2D> &command_path(commands->states);

    while (CSComplete != cs && command_path.empty()) {

        switch(cs) {

            case CSForwardDrive:

                car = ForwardDrive(car);

                // add the current state to the command list path
                command_path.push_back(car);

                // Ackerman model
                car = vehicle_model.NextState(car);

                break;

            case CSReverseDrive:

                car = ForwardDrive(car);

                // add the current state to the command list path
                command_path.push_back(car);

                // Ackerman model
                car = vehicle_model.NextState(car);

                break;

            case CSStopped:

                car = Stopped(car);

                // add the current state to the command list path
                command_path.push_back(car);

                // Ackerman model
                car = vehicle_model.NextState(car);

                break;

            case CSStandby:

                cs = CSStopped;

                break;

        }

    }

    return commands;

}

/**
 * Copyright (c) 2020, Román Cárdenas Rodríguez
 * ARSLab - Carleton University
 * GreenLSI - Polytechnic University of Madrid
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef CADMIUM_CELLDEVS_PANDEMIC_CELL_HPP
#define CADMIUM_CELLDEVS_PANDEMIC_CELL_HPP

#include <cmath>
#include <nlohmann/json.hpp>
#include <cadmium/celldevs/cell/grid_cell.hpp>

using nlohmann::json;
using namespace cadmium::celldevs;

/************************************/
/******COMPLEX STATE STRUCTURE*******/
/************************************/
struct sir {
    std::vector<float> age_divided_populations;
    unsigned int population;
    float susceptible;
    float infected;
    float recovered;
    sir() : population(0), age_divided_populations({ 0.0, 0.0, 0.0, 0.0, 0.0 }), susceptible(1), infected(0), recovered(0) {}  // a default constructor is required
    sir(unsigned int pop, std::vector<float> pops, float s, float i, float r) : population(pop), age_divided_populations(pops), susceptible(s), infected(i), recovered(r) {}
};
// Required for comparing states and detect any change
inline bool operator != (const sir &x, const sir &y) {
    if(x.population != y.population || x.age_divided_populations.size() != y.age_divided_populations.size() || x.susceptible != y.susceptible || x.infected != y.infected || x.recovered != y.recovered) {
        return true;
    }
    for(auto i = 0; i < x.age_divided_populations.size(); i++) {
        if(x.age_divided_populations[i] != y.age_divided_populations[i]) {
            return true;
        }
    }
    return false;
}
// Required if you want to use transport delay (priority queue has to sort messages somehow)
inline bool operator < (const sir& lhs, const sir& rhs){ return true; }

// Required for printing the state of the cell
std::ostream &operator << (std::ostream &os, const sir &x) {
    os << "<" << x.population << ",";
    
    for(auto sub_population: x.age_divided_populations) {
        os << sub_population << ",";
    }
    
    os << x.susceptible << "," << x.infected << "," << x.recovered <<">";
    return os;
}

// Required for creating SIR objects from JSON file
void from_json(const json& j, sir &s) {
    j.at("population").get_to(s.population);
    j.at("age_divided_populations").get_to(s.age_divided_populations);
    j.at("susceptible").get_to(s.susceptible);
    j.at("infected").get_to(s.infected);
    j.at("recovered").get_to(s.recovered);
}

/************************************/
/*****COMPLEX VICINITY STRUCTURE*****/
/************************************/
struct mc {
    std::vector<float> connection;
    std::vector<float> movement;
    mc() : connection({ 0 }), movement({ 0 }) {}  // a default constructor is required
    mc(std::vector<float> c, std::vector<float> m) : connection(c), movement(m) {}
};
// Required for creating movement-connection objects from JSON file
void from_json(const json& j, mc &m) {
    j.at("connection").get_to(m.connection);
    j.at("movement").get_to(m.movement);
}

/************************************/
/******COMPLEX CONFIG STRUCTURE******/
/************************************/
struct vr {
    std::vector<float> virulence;
    std::vector<float> recovery;
    vr(): virulence({0.6}), recovery({0.4}) {}
    vr(std::vector<float> v, std::vector<float> r): virulence(v), recovery(r) {}
};
void from_json(const json& j, vr &v) {
    j.at("virulence").get_to(v.virulence);
    j.at("recovery").get_to(v.recovery);
}

template <typename T>
class hoya_cell : public grid_cell<T, sir, mc> {
public:
    using grid_cell<T, sir, mc>::simulation_clock;
    using grid_cell<T, sir, mc>::state;
    using grid_cell<T, sir, mc>::map;
    using grid_cell<T, sir, mc>::neighbors;

    using config_type = vr;  // IMPORTANT FOR THE JSON
    std::vector<float> virulence;
    std::vector<float> recovery;

    hoya_cell() : grid_cell<T, sir, mc>() {}

    hoya_cell(cell_position const &cell_id, cell_unordered<mc> const &neighborhood, sir initial_state,
              cell_map<sir, mc> const &map_in, std::string const &delay_id, vr config) :
            grid_cell<T, sir, mc>(cell_id, neighborhood, initial_state, map_in, delay_id) {
        virulence = config.virulence;
        recovery = config.recovery;
    }

    // user must define this function. It returns the next cell state and its corresponding timeout
    sir local_computation() const override {
        sir res = state.current_state;
        float new_i = new_infections();
        float new_r = 0;
        for(int i = 0; i < res.age_divided_populations.size(); i++) {
            new_r += res.infected * res.age_divided_populations[i] * recovery[i];
        }
        res.recovered = std::round((res.recovered + new_r) * 100) / 100;
        res.infected = std::round((res.infected + new_i - new_r) * 100) / 100;
        res.susceptible = 1 - res.infected - res.recovered;
        return res;
    }
    // It returns the delay to communicate cell's new state.
    T output_delay(sir const &cell_state) const override {
        return 1;
    }

    float new_infections() const {
        float aux = 0;
        for(auto neighbor: neighbors) {
            sir n = state.neighbors_state.at(neighbor);
            mc v = state.neighbors_vicinity.at(neighbor);
            for(int i = 0; i < n.age_divided_populations.size(); i++) {
                aux += n.infected * (float) n.population * n.age_divided_populations[i] * v.movement[i] * v.connection[i] * virulence[i];
            }
        }
        sir s = state.current_state;
        return std::min(s.susceptible, s.susceptible * aux / (float) s.population);
    }
};

#endif //CADMIUM_CELLDEVS_PANDEMIC_CELL_HPP

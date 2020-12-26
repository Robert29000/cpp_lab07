// Copyright 2020 Your Name <your_email>

#include <server_helper.hpp>



void from_json(const json& j, suggest& s){
  j.at("id").get_to(s.id);
  j.at("name").get_to(s.name);
  j.at("cost").get_to(s.cost);
}

result::result(std::string& name_p, int cost_p):name(name_p), cost(cost_p), position(-1){
}

bool result::operator<(const result& r) const {
    return cost < r.cost;
}

void to_json(json& j, const result& r){
  j = json{{"text", r.name}, {"position", r.position}};
}

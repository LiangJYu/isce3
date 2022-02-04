// Copyright 2010-2021 Google LLC
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "min_cost_flow.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>

#include <isce3/except/Error.h>
#include <pyre/journal.h>

#include "graphs.h"
#include "max_flow.h"

namespace operations_research {

template <typename Graph, typename ArcFlowType, typename ArcScaledCostType>
GenericMinCostFlow<Graph, ArcFlowType, ArcScaledCostType>::GenericMinCostFlow(
    const Graph* graph)
    : graph_(graph),
      node_excess_(),
      node_potential_(),
      residual_arc_capacity_(),
      first_admissible_arc_(),
      active_nodes_(),
      epsilon_(0),
      alpha_(5),
      cost_scaling_factor_(1),
      scaled_arc_unit_cost_(),
      total_flow_cost_(0),
      status_(NOT_SOLVED),
      initial_node_excess_(),
      feasible_node_excess_(),
      feasibility_checked_(false),
      use_price_update_(false),
      check_feasibility_(true) {
  const NodeIndex max_num_nodes = Graphs<Graph>::NodeReservation(*graph_);
  if (max_num_nodes > 0) {
    node_excess_.Reserve(0, max_num_nodes - 1);
    node_excess_.SetAll(0);
    node_potential_.Reserve(0, max_num_nodes - 1);
    node_potential_.SetAll(0);
    first_admissible_arc_.Reserve(0, max_num_nodes - 1);
    first_admissible_arc_.SetAll(Graph::kNilArc);
    initial_node_excess_.Reserve(0, max_num_nodes - 1);
    initial_node_excess_.SetAll(0);
    feasible_node_excess_.Reserve(0, max_num_nodes - 1);
    feasible_node_excess_.SetAll(0);
  }
  const ArcIndex max_num_arcs = Graphs<Graph>::ArcReservation(*graph_);
  if (max_num_arcs > 0) {
    residual_arc_capacity_.Reserve(-max_num_arcs, max_num_arcs - 1);
    residual_arc_capacity_.SetAll(0);
    scaled_arc_unit_cost_.Reserve(-max_num_arcs, max_num_arcs - 1);
    scaled_arc_unit_cost_.SetAll(0);
  }
}

template <typename Graph, typename ArcFlowType, typename ArcScaledCostType>
void GenericMinCostFlow<Graph, ArcFlowType, ArcScaledCostType>::SetNodeSupply(
    NodeIndex node, FlowQuantity supply) {
  assert(graph_->IsNodeValid(node));
  node_excess_.Set(node, supply);
  initial_node_excess_.Set(node, supply);
  status_ = NOT_SOLVED;
  feasibility_checked_ = false;
}

template <typename Graph, typename ArcFlowType, typename ArcScaledCostType>
void GenericMinCostFlow<Graph, ArcFlowType, ArcScaledCostType>::SetArcUnitCost(
    ArcIndex arc, ArcScaledCostType unit_cost) {
  assert(IsArcDirect(arc));
  scaled_arc_unit_cost_.Set(arc, unit_cost);
  scaled_arc_unit_cost_.Set(Opposite(arc), -scaled_arc_unit_cost_[arc]);
  status_ = NOT_SOLVED;
  feasibility_checked_ = false;
}

template <typename Graph, typename ArcFlowType, typename ArcScaledCostType>
void GenericMinCostFlow<Graph, ArcFlowType, ArcScaledCostType>::SetArcCapacity(
    ArcIndex arc, ArcFlowType new_capacity) {
  assert(0 <= new_capacity);
  assert(IsArcDirect(arc));
  const FlowQuantity free_capacity = residual_arc_capacity_[arc];
  const FlowQuantity capacity_delta = new_capacity - Capacity(arc);
  if (capacity_delta == 0) {
    return;  // Nothing to do.
  }
  status_ = NOT_SOLVED;
  feasibility_checked_ = false;
  const FlowQuantity new_availability = free_capacity + capacity_delta;
  if (new_availability >= 0) {
    // The above condition is true when one of two following holds:
    // 1/ (capacity_delta > 0), meaning we are increasing the capacity
    // 2/ (capacity_delta < 0 && free_capacity + capacity_delta >= 0)
    //    meaning we are reducing the capacity, but that the capacity
    //    reduction is not larger than the free capacity.
    assert((capacity_delta > 0) ||
           (capacity_delta < 0 && new_availability >= 0));
    residual_arc_capacity_.Set(arc, new_availability);
    assert(0 <= residual_arc_capacity_[arc]);
  } else {
    // We have to reduce the flow on the arc, and update the excesses
    // accordingly.
    const FlowQuantity flow = residual_arc_capacity_[Opposite(arc)];
    const FlowQuantity flow_excess = flow - new_capacity;
    residual_arc_capacity_.Set(arc, 0);
    residual_arc_capacity_.Set(Opposite(arc), new_capacity);
    const NodeIndex tail = Tail(arc);
    node_excess_.Set(tail, node_excess_[tail] + flow_excess);
    const NodeIndex head = Head(arc);
    node_excess_.Set(head, node_excess_[head] - flow_excess);
    assert(0 <= residual_arc_capacity_[arc]);
    assert(0 <= residual_arc_capacity_[Opposite(arc)]);
  }
}

template <typename Graph, typename ArcFlowType, typename ArcScaledCostType>
void GenericMinCostFlow<Graph, ArcFlowType, ArcScaledCostType>::SetArcFlow(
    ArcIndex arc, ArcFlowType new_flow) {
  assert(IsArcValid(arc));
  const FlowQuantity capacity = Capacity(arc);
  assert(capacity >= new_flow);
  residual_arc_capacity_.Set(Opposite(arc), new_flow);
  residual_arc_capacity_.Set(arc, capacity - new_flow);
  status_ = NOT_SOLVED;
  feasibility_checked_ = false;
}

template <typename Graph, typename ArcFlowType, typename ArcScaledCostType>
bool GenericMinCostFlow<Graph, ArcFlowType,
                        ArcScaledCostType>::CheckInputConsistency() const {
  FlowQuantity total_supply = 0;
  uint64_t max_capacity = 0;  // uint64_t because it is positive and will be
                              // used to check against FlowQuantity overflows.
  for (ArcIndex arc = 0; arc < graph_->num_arcs(); ++arc) {
    const uint64_t capacity =
        static_cast<uint64_t>(residual_arc_capacity_[arc]);
    max_capacity = std::max(capacity, max_capacity);
  }
  uint64_t total_flow = 0;  // uint64_t for the same reason as max_capacity.
  for (NodeIndex node = 0; node < graph_->num_nodes(); ++node) {
    const FlowQuantity excess = node_excess_[node];
    total_supply += excess;
    if (excess > 0) {
      total_flow += excess;
      if (std::numeric_limits<FlowQuantity>::max() <
          max_capacity + total_flow) {
        pyre::journal::firewall_t channel("isce3.unwrap.ortools.min_cost_flow");
        channel << pyre::journal::at(__HERE__)
                << "Input consistency error: max capacity + flow exceed "
                   "precision"
                << pyre::journal::endl;
        return false;
      }
    }
  }
  if (total_supply != 0) {
    pyre::journal::firewall_t channel("isce3.unwrap.ortools.min_cost_flow");
    channel << pyre::journal::at(__HERE__)
            << "Input consistency error: unbalanced problem"
            << pyre::journal::endl;
    return false;
  }
  return true;
}

template <typename Graph, typename ArcFlowType, typename ArcScaledCostType>
bool GenericMinCostFlow<Graph, ArcFlowType, ArcScaledCostType>::CheckResult()
    const {
  for (NodeIndex node = 0; node < graph_->num_nodes(); ++node) {
    if (node_excess_[node] != 0) {
      pyre::journal::firewall_t channel("isce3.unwrap.ortools.min_cost_flow");
      channel << pyre::journal::at(__HERE__)
              << "node_excess_[" << node << "] != 0"
              << pyre::journal::endl;
      return false;
    }
    for (OutgoingOrOppositeIncomingArcIterator it(*graph_, node); it.Ok();
         it.Next()) {
      const ArcIndex arc = it.Index();
      bool ok = true;
      if (residual_arc_capacity_[arc] < 0) {
        pyre::journal::firewall_t channel("isce3.unwrap.ortools.min_cost_flow");
        channel << pyre::journal::at(__HERE__)
                << "residual_arc_capacity_[" << arc << "] < 0"
                << pyre::journal::endl;
        ok = false;
      }
      if (residual_arc_capacity_[arc] > 0 && ReducedCost(arc) < -epsilon_) {
        pyre::journal::firewall_t channel("isce3.unwrap.ortools.min_cost_flow");
        channel << pyre::journal::at(__HERE__)
                << "residual_arc_capacity_[" << arc
                << "] > 0 && ReducedCost(" << arc << ") < " << -epsilon_
                << ". (epsilon_ = " << epsilon_ << ")."
                << pyre::journal::endl;
        ok = false;
      }
      if (!ok) {
        pyre::journal::firewall_t channel("isce3.unwrap.ortools.min_cost_flow");
        channel << pyre::journal::at(__HERE__)
                << DebugString("CheckResult ", arc)
                << pyre::journal::endl;
        return false;
      }
    }
  }
  return true;
}

template <typename Graph, typename ArcFlowType, typename ArcScaledCostType>
bool GenericMinCostFlow<Graph, ArcFlowType, ArcScaledCostType>::CheckCostRange()
    const {
  CostValue min_cost_magnitude = std::numeric_limits<CostValue>::max();
  CostValue max_cost_magnitude = 0;
  // Traverse the initial arcs of the graph:
  for (ArcIndex arc = 0; arc < graph_->num_arcs(); ++arc) {
    const CostValue cost_magnitude = std::abs(scaled_arc_unit_cost_[arc]);
    max_cost_magnitude = std::max(max_cost_magnitude, cost_magnitude);
    if (cost_magnitude != 0.0) {
      min_cost_magnitude = std::min(min_cost_magnitude, cost_magnitude);
    }
  }
  pyre::journal::debug_t debug("isce3.unwrap.ortools.min_cost_flow");
  debug << pyre::journal::at(__HERE__)
        << "Min cost magnitude = " << min_cost_magnitude
        << ", Max cost magnitude = " << max_cost_magnitude
        << pyre::journal::endl;
#if !defined(_MSC_VER)
  if (log(std::numeric_limits<CostValue>::max()) <
      log(max_cost_magnitude + 1) + log(graph_->num_nodes() + 1)) {
    pyre::journal::firewall_t firewall("isce3.unwrap.ortools.min_cost_flow");
    firewall << pyre::journal::at(__HERE__)
             << "Maximum cost magnitude " << max_cost_magnitude
             << " is too high for the number of nodes. Try changing the data."
             << pyre::journal::endl;
    return false;
  }
#endif
  return true;
}

template <typename Graph, typename ArcFlowType, typename ArcScaledCostType>
bool GenericMinCostFlow<Graph, ArcFlowType, ArcScaledCostType>::
    CheckRelabelPrecondition(NodeIndex node) const {
  // Note that the classical Relabel precondition assumes IsActive(node), i.e.,
  // the node_excess_[node] > 0. However, to implement the Push Look-Ahead
  // heuristic, we can relax this condition as explained in the section 4.3 of
  // the article "An Efficient Implementation of a Scaling Minimum-Cost Flow
  // Algorithm", A.V. Goldberg, Journal of Algorithms 22(1), January 1997, pp.
  // 1-29.
  assert(node_excess_[node] >= 0);
  for (OutgoingOrOppositeIncomingArcIterator it(*graph_, node); it.Ok();
       it.Next()) {
#ifndef NDEBUG
    const ArcIndex arc = it.Index();
    assert(!IsAdmissible(arc) && DebugString("CheckRelabelPrecondition:", arc));
#endif
  }
  return true;
}

template <typename Graph, typename ArcFlowType, typename ArcScaledCostType>
std::string
GenericMinCostFlow<Graph, ArcFlowType, ArcScaledCostType>::DebugString(
    const std::string& context, ArcIndex arc) const {
  const NodeIndex tail = Tail(arc);
  const NodeIndex head = Head(arc);
  // Reduced cost is computed directly without calling ReducedCost to avoid
  // recursive calls between ReducedCost and DebugString in case a DCHECK in
  // ReducedCost fails.
  const CostValue reduced_cost = scaled_arc_unit_cost_[arc] +
                                 node_potential_[tail] - node_potential_[head];
  return std::string(context) + " Arc " + std::to_string(arc)
    + ", from " + std::to_string(tail) + " to " + std::to_string(head)
    + ", Capacity = " + std::to_string(Capacity(arc)) + ", Residual capacity = "
    + std::to_string(static_cast<FlowQuantity>(residual_arc_capacity_[arc]))
    + ", Flow = residual capacity for reverse arc = " + std::to_string(Flow(arc))
    + ", Height(tail) = " + std::to_string(node_potential_[tail])
    + ", Height(head) = " + std::to_string(node_potential_[head])
    + ", Excess(tail) = " + std::to_string(node_excess_[tail])
    + ", Excess(head) = " + std::to_string(node_excess_[head]) + ", Cost = "
    + std::to_string(static_cast<CostValue>(scaled_arc_unit_cost_[arc]))
    + ", Reduced cost = " + std::to_string(reduced_cost) + ", ";
}

template <typename Graph, typename ArcFlowType, typename ArcScaledCostType>
bool GenericMinCostFlow<Graph, ArcFlowType, ArcScaledCostType>::
    CheckFeasibility(std::vector<NodeIndex>* const infeasible_supply_node,
                     std::vector<NodeIndex>* const infeasible_demand_node) {
  // Create a new graph, which is a copy of graph_, with the following
  // modifications:
  // Two nodes are added: a source and a sink.
  // The source is linked to each supply node (whose supply > 0) by an arc whose
  // capacity is equal to the supply at the supply node.
  // The sink is linked to each demand node (whose supply < 0) by an arc whose
  // capacity is the demand (-supply) at the demand node.
  // There are no supplies or demands or costs in the graph, as we will run
  // max-flow.
  // TODO(user): make it possible to share a graph by MaxFlow and MinCostFlow.
  // For this it is necessary to make StarGraph resizable.
  feasibility_checked_ = false;
  ArcIndex num_extra_arcs = 0;
  for (NodeIndex node = 0; node < graph_->num_nodes(); ++node) {
    if (initial_node_excess_[node] != 0) {
      ++num_extra_arcs;
    }
  }
  const NodeIndex num_nodes_in_max_flow = graph_->num_nodes() + 2;
  const ArcIndex num_arcs_in_max_flow = graph_->num_arcs() + num_extra_arcs;
  const NodeIndex source = num_nodes_in_max_flow - 2;
  const NodeIndex sink = num_nodes_in_max_flow - 1;
  StarGraph checker_graph(num_nodes_in_max_flow, num_arcs_in_max_flow);
  MaxFlow checker(&checker_graph, source, sink);
  checker.SetCheckInput(false);
  checker.SetCheckResult(false);
  // Copy graph_ to checker_graph.
  for (ArcIndex arc = 0; arc < graph_->num_arcs(); ++arc) {
    const ArcIndex new_arc =
        checker_graph.AddArc(graph_->Tail(arc), graph_->Head(arc));
    assert(arc == new_arc);
    checker.SetArcCapacity(new_arc, Capacity(arc));
  }
  FlowQuantity total_demand = 0;
  FlowQuantity total_supply = 0;
  // Create the source-to-supply node arcs and the demand-node-to-sink arcs.
  for (NodeIndex node = 0; node < graph_->num_nodes(); ++node) {
    const FlowQuantity supply = initial_node_excess_[node];
    if (supply > 0) {
      const ArcIndex new_arc = checker_graph.AddArc(source, node);
      checker.SetArcCapacity(new_arc, supply);
      total_supply += supply;
    } else if (supply < 0) {
      const ArcIndex new_arc = checker_graph.AddArc(node, sink);
      checker.SetArcCapacity(new_arc, -supply);
      total_demand -= supply;
    }
  }
  if (total_supply != total_demand) {
    pyre::journal::firewall_t channel("isce3.unwrap.ortools.min_cost_flow");
    channel << pyre::journal::at(__HERE__)
            << "total_supply(" << total_supply << ") != total_demand("
            << total_demand << ")."
            << pyre::journal::endl;
    return false;
  }
  if (!checker.Solve()) {
    pyre::journal::firewall_t channel("isce3.unwrap.ortools.min_cost_flow");
    channel << pyre::journal::at(__HERE__)
            << "Max flow could not be computed."
            << pyre::journal::endl;
    return false;
  }
  const FlowQuantity optimal_max_flow = checker.GetOptimalFlow();
  feasible_node_excess_.SetAll(0);
  for (StarGraph::OutgoingArcIterator it(checker_graph, source); it.Ok();
       it.Next()) {
    const ArcIndex arc = it.Index();
    const NodeIndex node = checker_graph.Head(arc);
    const FlowQuantity flow = checker.Flow(arc);
    feasible_node_excess_.Set(node, flow);
    if (infeasible_supply_node != nullptr) {
      infeasible_supply_node->push_back(node);
    }
  }
  for (StarGraph::IncomingArcIterator it(checker_graph, sink); it.Ok();
       it.Next()) {
    const ArcIndex arc = it.Index();
    const NodeIndex node = checker_graph.Tail(arc);
    const FlowQuantity flow = checker.Flow(arc);
    feasible_node_excess_.Set(node, -flow);
    if (infeasible_demand_node != nullptr) {
      infeasible_demand_node->push_back(node);
    }
  }
  feasibility_checked_ = true;
  return optimal_max_flow == total_supply;
}

template <typename Graph, typename ArcFlowType, typename ArcScaledCostType>
bool GenericMinCostFlow<Graph, ArcFlowType, ArcScaledCostType>::MakeFeasible() {
  if (!feasibility_checked_) {
    return false;
  }
  for (NodeIndex node = 0; node < graph_->num_nodes(); ++node) {
    const FlowQuantity excess = feasible_node_excess_[node];
    node_excess_.Set(node, excess);
    initial_node_excess_.Set(node, excess);
  }
  return true;
}

template <typename Graph, typename ArcFlowType, typename ArcScaledCostType>
FlowQuantity GenericMinCostFlow<Graph, ArcFlowType, ArcScaledCostType>::Flow(
    ArcIndex arc) const {
  if (IsArcDirect(arc)) {
    return residual_arc_capacity_[Opposite(arc)];
  } else {
    return -residual_arc_capacity_[arc];
  }
}

// We use the equations given in the comment of residual_arc_capacity_.
template <typename Graph, typename ArcFlowType, typename ArcScaledCostType>
FlowQuantity
GenericMinCostFlow<Graph, ArcFlowType, ArcScaledCostType>::Capacity(
    ArcIndex arc) const {
  if (IsArcDirect(arc)) {
    return residual_arc_capacity_[arc] + residual_arc_capacity_[Opposite(arc)];
  } else {
    return 0;
  }
}

template <typename Graph, typename ArcFlowType, typename ArcScaledCostType>
CostValue GenericMinCostFlow<Graph, ArcFlowType, ArcScaledCostType>::UnitCost(
    ArcIndex arc) const {
  assert(IsArcValid(arc));
  assert(uint64_t{1} == cost_scaling_factor_);
  return scaled_arc_unit_cost_[arc];
}

template <typename Graph, typename ArcFlowType, typename ArcScaledCostType>
FlowQuantity GenericMinCostFlow<Graph, ArcFlowType, ArcScaledCostType>::Supply(
    NodeIndex node) const {
  assert(graph_->IsNodeValid(node));
  return node_excess_[node];
}

template <typename Graph, typename ArcFlowType, typename ArcScaledCostType>
FlowQuantity
GenericMinCostFlow<Graph, ArcFlowType, ArcScaledCostType>::InitialSupply(
    NodeIndex node) const {
  return initial_node_excess_[node];
}

template <typename Graph, typename ArcFlowType, typename ArcScaledCostType>
FlowQuantity
GenericMinCostFlow<Graph, ArcFlowType, ArcScaledCostType>::FeasibleSupply(
    NodeIndex node) const {
  return feasible_node_excess_[node];
}

template <typename Graph, typename ArcFlowType, typename ArcScaledCostType>
bool GenericMinCostFlow<Graph, ArcFlowType, ArcScaledCostType>::IsAdmissible(
    ArcIndex arc) const {
  return FastIsAdmissible(arc, node_potential_[Tail(arc)]);
}

template <typename Graph, typename ArcFlowType, typename ArcScaledCostType>
bool GenericMinCostFlow<Graph, ArcFlowType, ArcScaledCostType>::
    FastIsAdmissible(ArcIndex arc, CostValue tail_potential) const {
  assert(node_potential_[Tail(arc)] == tail_potential);
  return residual_arc_capacity_[arc] > 0 &&
         FastReducedCost(arc, tail_potential) < 0;
}

template <typename Graph, typename ArcFlowType, typename ArcScaledCostType>
bool GenericMinCostFlow<Graph, ArcFlowType, ArcScaledCostType>::IsActive(
    NodeIndex node) const {
  return node_excess_[node] > 0;
}

template <typename Graph, typename ArcFlowType, typename ArcScaledCostType>
CostValue
GenericMinCostFlow<Graph, ArcFlowType, ArcScaledCostType>::ReducedCost(
    ArcIndex arc) const {
  return FastReducedCost(arc, node_potential_[Tail(arc)]);
}

template <typename Graph, typename ArcFlowType, typename ArcScaledCostType>
CostValue
GenericMinCostFlow<Graph, ArcFlowType, ArcScaledCostType>::FastReducedCost(
    ArcIndex arc, CostValue tail_potential) const {
  assert(node_potential_[Tail(arc)] == tail_potential);
  assert(graph_->IsNodeValid(Tail(arc)));
  assert(graph_->IsNodeValid(Head(arc)));
  assert(node_potential_[Tail(arc)] <= 0 && DebugString("ReducedCost:", arc));
  assert(node_potential_[Head(arc)] <= 0 && DebugString("ReducedCost:", arc));
  return scaled_arc_unit_cost_[arc] + tail_potential -
         node_potential_[Head(arc)];
}

template <typename Graph, typename ArcFlowType, typename ArcScaledCostType>
typename GenericMinCostFlow<Graph, ArcFlowType, ArcScaledCostType>::ArcIndex
GenericMinCostFlow<Graph, ArcFlowType, ArcScaledCostType>::
    GetFirstOutgoingOrOppositeIncomingArc(NodeIndex node) const {
  OutgoingOrOppositeIncomingArcIterator arc_it(*graph_, node);
  return arc_it.Index();
}

template <typename Graph, typename ArcFlowType, typename ArcScaledCostType>
bool GenericMinCostFlow<Graph, ArcFlowType, ArcScaledCostType>::Solve() {
  status_ = NOT_SOLVED;
  if (!CheckInputConsistency()) {
    status_ = UNBALANCED;
    return false;
  }
  if (!CheckCostRange()) {
    status_ = BAD_COST_RANGE;
    return false;
  }
  if (check_feasibility_ && !CheckFeasibility(nullptr, nullptr)) {
    status_ = INFEASIBLE;
    return false;
  }
  node_potential_.SetAll(0);
  ResetFirstAdmissibleArcs();
  ScaleCosts();
  Optimize();
  if (!CheckResult()) {
    status_ = BAD_RESULT;
    UnscaleCosts();
    return false;
  }
  UnscaleCosts();
  if (status_ != OPTIMAL) {
    pyre::journal::firewall_t channel("isce3.unwrap.ortools.min_cost_flow");
    channel << pyre::journal::at(__HERE__)
            << "Status != OPTIMAL"
            << pyre::journal::endl;
    total_flow_cost_ = 0;
    return false;
  }
  total_flow_cost_ = 0;
  for (ArcIndex arc = 0; arc < graph_->num_arcs(); ++arc) {
    const FlowQuantity flow_on_arc = residual_arc_capacity_[Opposite(arc)];
    total_flow_cost_ += scaled_arc_unit_cost_[arc] * flow_on_arc;
  }
  status_ = OPTIMAL;
  return true;
}

template <typename Graph, typename ArcFlowType, typename ArcScaledCostType>
void GenericMinCostFlow<Graph, ArcFlowType,
                        ArcScaledCostType>::ResetFirstAdmissibleArcs() {
  for (NodeIndex node = 0; node < graph_->num_nodes(); ++node) {
    first_admissible_arc_.Set(node,
                              GetFirstOutgoingOrOppositeIncomingArc(node));
  }
}

template <typename Graph, typename ArcFlowType, typename ArcScaledCostType>
void GenericMinCostFlow<Graph, ArcFlowType, ArcScaledCostType>::ScaleCosts() {
  cost_scaling_factor_ = graph_->num_nodes() + 1;
  epsilon_ = 1LL;
  pyre::journal::debug_t channel("isce3.unwrap.ortools.min_cost_flow");
  channel << pyre::journal::at(__HERE__)
          << "Number of nodes in the graph = " << graph_->num_nodes()
          << pyre::journal::endl
          << "Number of arcs in the graph = " << graph_->num_arcs()
          << pyre::journal::endl;
  for (ArcIndex arc = 0; arc < graph_->num_arcs(); ++arc) {
    const CostValue cost = scaled_arc_unit_cost_[arc] * cost_scaling_factor_;
    scaled_arc_unit_cost_.Set(arc, cost);
    scaled_arc_unit_cost_.Set(Opposite(arc), -cost);
    epsilon_ = std::max(epsilon_, std::abs(cost));
  }
  channel << pyre::journal::at(__HERE__)
          << "Cost scaling factor = " << cost_scaling_factor_
          << pyre::journal::endl
          << "Initial epsilon = " << epsilon_
          << pyre::journal::endl;
}

template <typename Graph, typename ArcFlowType, typename ArcScaledCostType>
void GenericMinCostFlow<Graph, ArcFlowType, ArcScaledCostType>::UnscaleCosts() {
  for (ArcIndex arc = 0; arc < graph_->num_arcs(); ++arc) {
    const CostValue cost = scaled_arc_unit_cost_[arc] / cost_scaling_factor_;
    scaled_arc_unit_cost_.Set(arc, cost);
    scaled_arc_unit_cost_.Set(Opposite(arc), -cost);
  }
  cost_scaling_factor_ = 1;
}

template <typename Graph, typename ArcFlowType, typename ArcScaledCostType>
void GenericMinCostFlow<Graph, ArcFlowType, ArcScaledCostType>::Optimize() {
  const CostValue kEpsilonMin = 1LL;
  num_relabels_since_last_price_update_ = 0;
  do {
    // Avoid epsilon_ == 0.
    epsilon_ = std::max(epsilon_ / alpha_, kEpsilonMin);
    pyre::journal::debug_t channel("isce3.unwrap.ortools.min_cost_flow");
    channel << pyre::journal::at(__HERE__)
            << "Epsilon changed to: " << epsilon_
            << pyre::journal::endl;
    Refine();
  } while (epsilon_ != 1LL && status_ != INFEASIBLE);
  if (status_ == NOT_SOLVED) {
    status_ = OPTIMAL;
  }
}

template <typename Graph, typename ArcFlowType, typename ArcScaledCostType>
void GenericMinCostFlow<Graph, ArcFlowType,
                        ArcScaledCostType>::SaturateAdmissibleArcs() {
  for (NodeIndex node = 0; node < graph_->num_nodes(); ++node) {
    const CostValue tail_potential = node_potential_[node];
    for (OutgoingOrOppositeIncomingArcIterator it(*graph_, node,
                                                  first_admissible_arc_[node]);
         it.Ok(); it.Next()) {
      const ArcIndex arc = it.Index();
      if (FastIsAdmissible(arc, tail_potential)) {
        FastPushFlow(residual_arc_capacity_[arc], arc, node);
      }
    }

    // We just saturated all the admissible arcs, so there are no arcs with a
    // positive residual capacity that are incident to the current node.
    // Moreover, during the course of the algorithm, if the residual capacity of
    // such an arc becomes positive again, then the arc is still not admissible
    // until we relabel the node (because the reverse arc was admissible for
    // this to happen). In conclusion, the optimization below is correct.
    first_admissible_arc_[node] = Graph::kNilArc;
  }
}

template <typename Graph, typename ArcFlowType, typename ArcScaledCostType>
void GenericMinCostFlow<Graph, ArcFlowType, ArcScaledCostType>::PushFlow(
    FlowQuantity flow, ArcIndex arc) {
  FastPushFlow(flow, arc, Tail(arc));
}

template <typename Graph, typename ArcFlowType, typename ArcScaledCostType>
void GenericMinCostFlow<Graph, ArcFlowType, ArcScaledCostType>::FastPushFlow(
    FlowQuantity flow, ArcIndex arc, NodeIndex tail) {
  assert(Tail(arc) == tail);
  assert(residual_arc_capacity_[arc] > 0);
  assert(flow <= residual_arc_capacity_[arc]);
  // Reduce the residual capacity on the arc by flow.
  residual_arc_capacity_.Set(arc, residual_arc_capacity_[arc] - flow);
  // Increase the residual capacity on the opposite arc by flow.
  const ArcIndex opposite = Opposite(arc);
  residual_arc_capacity_.Set(opposite, residual_arc_capacity_[opposite] + flow);
  // Update the excesses at the tail and head of the arc.
  node_excess_.Set(tail, node_excess_[tail] - flow);
  const NodeIndex head = Head(arc);
  node_excess_.Set(head, node_excess_[head] + flow);
}

template <typename Graph, typename ArcFlowType, typename ArcScaledCostType>
void GenericMinCostFlow<Graph, ArcFlowType,
                        ArcScaledCostType>::InitializeActiveNodeStack() {
  assert(active_nodes_.empty());
  for (NodeIndex node = 0; node < graph_->num_nodes(); ++node) {
    if (IsActive(node)) {
      active_nodes_.push(node);
    }
  }
}

template <typename Graph, typename ArcFlowType, typename ArcScaledCostType>
void GenericMinCostFlow<Graph, ArcFlowType, ArcScaledCostType>::UpdatePrices() {

  // The algorithm works as follows. Start with a set of nodes S containing all
  // the nodes with negative excess. Expand the set along reverse admissible
  // arcs. If at the end, the complement of S contains at least one node with
  // positive excess, relabel all the nodes in the complement of S by
  // subtracting epsilon from their current potential. See the paper cited in
  // the .h file.
  //
  // After this relabeling is done, the heuristic is reapplied by extending S as
  // much as possible, relabeling the complement of S, and so on until there is
  // no node with positive excess that is not in S. Note that this is not
  // described in the paper.
  //
  // Note(user): The triggering mechanism of this UpdatePrices() is really
  // important; if it is not done properly it may degrade performance!

  // This represents the set S.
  const NodeIndex num_nodes = graph_->num_nodes();
  std::vector<NodeIndex> bfs_queue;
  std::vector<bool> node_in_queue(num_nodes, false);

  // This is used to update the potential of the nodes not in S.
  const CostValue kMinCostValue = std::numeric_limits<CostValue>::min();
  std::vector<CostValue> min_non_admissible_potential(num_nodes, kMinCostValue);
  std::vector<NodeIndex> nodes_to_process;

  // Sum of the positive excesses out of S, used for early exit.
  FlowQuantity remaining_excess = 0;

  // First consider the nodes which have a negative excess.
  for (NodeIndex node = 0; node < num_nodes; ++node) {
    if (node_excess_[node] < 0) {
      bfs_queue.push_back(node);
      node_in_queue[node] = true;

      // This uses the fact that the sum of excesses is always 0.
      remaining_excess -= node_excess_[node];
    }
  }

  // All the nodes not yet in the bfs_queue will have their potential changed by
  // +potential_delta (which becomes more and more negative at each pass). This
  // update is applied when a node is pushed into the queue and at the end of
  // the function for the nodes that are still unprocessed.
  CostValue potential_delta = 0;

  int queue_index = 0;
  while (remaining_excess > 0) {
    // Reverse BFS that expands S as much as possible in the reverse admissible
    // graph. Once S cannot be expanded anymore, perform a relabeling on the
    // nodes not in S but that can reach it in one arc and try to expand S
    // again.
    for (; queue_index < bfs_queue.size(); ++queue_index) {
      assert(num_nodes >= bfs_queue.size());
      const NodeIndex node = bfs_queue[queue_index];
      for (OutgoingOrOppositeIncomingArcIterator it(*graph_, node); it.Ok();
           it.Next()) {
        const NodeIndex head = Head(it.Index());
        if (node_in_queue[head]) continue;
        const ArcIndex opposite_arc = Opposite(it.Index());
        if (residual_arc_capacity_[opposite_arc] > 0) {
          node_potential_[head] += potential_delta;
          if (ReducedCost(opposite_arc) < 0) {
            assert(IsAdmissible(opposite_arc));

            // TODO(user): Try to steal flow if node_excess_[head] > 0.
            // An initial experiment didn't show a big speedup though.

            remaining_excess -= node_excess_[head];
            if (remaining_excess == 0) {
              node_potential_[head] -= potential_delta;
              break;
            }
            bfs_queue.push_back(head);
            node_in_queue[head] = true;
            if (potential_delta < 0) {
              first_admissible_arc_[head] =
                  GetFirstOutgoingOrOppositeIncomingArc(head);
            }
          } else {
            // The opposite_arc is not admissible but is in the residual graph;
            // this updates its min_non_admissible_potential.
            node_potential_[head] -= potential_delta;
            if (min_non_admissible_potential[head] == kMinCostValue) {
              nodes_to_process.push_back(head);
            }
            min_non_admissible_potential[head] = std::max(
                min_non_admissible_potential[head],
                node_potential_[node] - scaled_arc_unit_cost_[opposite_arc]);
          }
        }
      }
      if (remaining_excess == 0) break;
    }
    if (remaining_excess == 0) break;

    // Decrease by as much as possible instead of decreasing by epsilon.
    // TODO(user): Is it worth the extra loop?
    CostValue max_potential_diff = kMinCostValue;
    for (int i = 0; i < nodes_to_process.size(); ++i) {
      const NodeIndex node = nodes_to_process[i];
      if (node_in_queue[node]) continue;
      max_potential_diff =
          std::max(max_potential_diff,
                   min_non_admissible_potential[node] - node_potential_[node]);
      if (max_potential_diff == potential_delta) break;
    }
    assert(max_potential_diff <= potential_delta);
    potential_delta = max_potential_diff - epsilon_;

    // Loop over nodes_to_process_ and for each node, apply the first of the
    // rules below that match or leave it in the queue for later iteration:
    // - Remove it if it is already in the queue.
    // - If the node is connected to S by an admissible arc after it is
    //   relabeled by +potential_delta, add it to bfs_queue_ and remove it from
    //   nodes_to_process.
    int index = 0;
    for (int i = 0; i < nodes_to_process.size(); ++i) {
      const NodeIndex node = nodes_to_process[i];
      if (node_in_queue[node]) continue;
      if (node_potential_[node] + potential_delta <
          min_non_admissible_potential[node]) {
        node_potential_[node] += potential_delta;
        first_admissible_arc_[node] =
            GetFirstOutgoingOrOppositeIncomingArc(node);
        bfs_queue.push_back(node);
        node_in_queue[node] = true;
        remaining_excess -= node_excess_[node];
        continue;
      }

      // Keep the node for later iteration.
      nodes_to_process[index] = node;
      ++index;
    }
    nodes_to_process.resize(index);
  }

  // Update the potentials of the nodes not yet processed.
  if (potential_delta == 0) return;
  for (NodeIndex node = 0; node < num_nodes; ++node) {
    if (!node_in_queue[node]) {
      node_potential_[node] += potential_delta;
      first_admissible_arc_[node] = GetFirstOutgoingOrOppositeIncomingArc(node);
    }
  }
}

template <typename Graph, typename ArcFlowType, typename ArcScaledCostType>
void GenericMinCostFlow<Graph, ArcFlowType, ArcScaledCostType>::Refine() {
  SaturateAdmissibleArcs();
  InitializeActiveNodeStack();

  const NodeIndex num_nodes = graph_->num_nodes();
  while (status_ != INFEASIBLE && !active_nodes_.empty()) {
    // TODO(user): Experiment with different factors in front of num_nodes.
    if (num_relabels_since_last_price_update_ >= num_nodes) {
      num_relabels_since_last_price_update_ = 0;
      if (use_price_update_) {
        UpdatePrices();
      }
    }
    const NodeIndex node = active_nodes_.top();
    active_nodes_.pop();
    assert(IsActive(node));
    Discharge(node);
  }
}

template <typename Graph, typename ArcFlowType, typename ArcScaledCostType>
void GenericMinCostFlow<Graph, ArcFlowType, ArcScaledCostType>::Discharge(
    NodeIndex node) {
  do {
    // The node is initially active, and we exit as soon as it becomes
    // inactive.
    assert(IsActive(node));
    const CostValue tail_potential = node_potential_[node];
    for (OutgoingOrOppositeIncomingArcIterator it(*graph_, node,
                                                  first_admissible_arc_[node]);
         it.Ok(); it.Next()) {
      const ArcIndex arc = it.Index();
      if (FastIsAdmissible(arc, tail_potential)) {
        const NodeIndex head = Head(arc);
        if (!LookAhead(arc, tail_potential, head)) continue;
        const bool head_active_before_push = IsActive(head);
        const FlowQuantity delta =
            std::min(node_excess_[node],
                     static_cast<FlowQuantity>(residual_arc_capacity_[arc]));
        FastPushFlow(delta, arc, node);
        if (IsActive(head) && !head_active_before_push) {
          active_nodes_.push(head);
        }
        if (node_excess_[node] == 0) {
          // arc may still be admissible.
          first_admissible_arc_.Set(node, arc);
          return;
        }
      }
    }
    Relabel(node);
  } while (status_ != INFEASIBLE);
}

template <typename Graph, typename ArcFlowType, typename ArcScaledCostType>
bool GenericMinCostFlow<Graph, ArcFlowType, ArcScaledCostType>::LookAhead(
    ArcIndex in_arc, CostValue in_tail_potential, NodeIndex node) {
  assert(Head(in_arc) == node);
  assert(node_potential_[Tail(in_arc)] == in_tail_potential);
  if (node_excess_[node] < 0) return true;
  const CostValue tail_potential = node_potential_[node];
  for (OutgoingOrOppositeIncomingArcIterator it(*graph_, node,
                                                first_admissible_arc_[node]);
       it.Ok(); it.Next()) {
    const ArcIndex arc = it.Index();
    if (FastIsAdmissible(arc, tail_potential)) {
      first_admissible_arc_.Set(node, arc);
      return true;
    }
  }

  // The node we looked ahead has no admissible arc at its current potential.
  // We relabel it and return true if the original arc is still admissible.
  Relabel(node);
  return FastIsAdmissible(in_arc, in_tail_potential);
}

template <typename Graph, typename ArcFlowType, typename ArcScaledCostType>
void GenericMinCostFlow<Graph, ArcFlowType, ArcScaledCostType>::Relabel(
    NodeIndex node) {
  assert(CheckRelabelPrecondition(node));
  ++num_relabels_since_last_price_update_;

  // By setting node_potential_[node] to the guaranteed_new_potential we are
  // sure to keep epsilon-optimality of the pseudo-flow. Note that we could
  // return right away with this value, but we prefer to check that this value
  // will lead to at least one admissible arc, and if not, to decrease the
  // potential as much as possible.
  const CostValue guaranteed_new_potential = node_potential_[node] - epsilon_;

  // This will be updated to contain the minimum node potential for which
  // the node has no admissible arc. We know that:
  // - min_non_admissible_potential <= node_potential_[node]
  // - We can set the new node potential to min_non_admissible_potential -
  //   epsilon_ and still keep the epsilon-optimality of the pseudo flow.
  const CostValue kMinCostValue = std::numeric_limits<CostValue>::min();
  CostValue min_non_admissible_potential = kMinCostValue;

  // The following variables help setting the first_admissible_arc_[node] to a
  // value different from GetFirstOutgoingOrOppositeIncomingArc(node) which
  // avoids looking again at some arcs.
  CostValue previous_min_non_admissible_potential = kMinCostValue;
  ArcIndex first_arc = Graph::kNilArc;

  for (OutgoingOrOppositeIncomingArcIterator it(*graph_, node); it.Ok();
       it.Next()) {
    const ArcIndex arc = it.Index();
    if (residual_arc_capacity_[arc] > 0) {
      const CostValue min_non_admissible_potential_for_arc =
          node_potential_[Head(arc)] - scaled_arc_unit_cost_[arc];
      if (min_non_admissible_potential_for_arc > min_non_admissible_potential) {
        if (min_non_admissible_potential_for_arc > guaranteed_new_potential) {
          // We found an admissible arc for the guaranteed_new_potential. We
          // stop right now instead of trying to compute the minimum possible
          // new potential that keeps the epsilon-optimality of the pseudo flow.
          node_potential_.Set(node, guaranteed_new_potential);
          first_admissible_arc_.Set(node, arc);
          return;
        }
        previous_min_non_admissible_potential = min_non_admissible_potential;
        min_non_admissible_potential = min_non_admissible_potential_for_arc;
        first_arc = arc;
      }
    }
  }

  // No admissible arc leaves this node!
  if (min_non_admissible_potential == kMinCostValue) {
    if (node_excess_[node] != 0) {
      // Note that this infeasibility detection is incomplete.
      // Only max flow can detect that a min-cost flow problem is infeasible.
      status_ = INFEASIBLE;
      pyre::journal::info_t channel("isce3.unwrap.ortools.min_cost_flow");
      channel << pyre::journal::at(__HERE__) << "Infeasible problem."
              << pyre::journal::endl;
    } else {
      // This source saturates all its arcs, we can actually decrease the
      // potential by as much as we want.
      // TODO(user): Set it to a minimum value, but be careful of overflow.
      node_potential_.Set(node, guaranteed_new_potential);
      first_admissible_arc_.Set(node,
                                GetFirstOutgoingOrOppositeIncomingArc(node));
    }
    return;
  }

  // We decrease the potential as much as possible, but we do not know the first
  // admissible arc (most of the time). Keeping the
  // previous_min_non_admissible_potential makes it faster by a few percent.
  const CostValue new_potential = min_non_admissible_potential - epsilon_;
  node_potential_.Set(node, new_potential);
  if (previous_min_non_admissible_potential <= new_potential) {
    first_admissible_arc_.Set(node, first_arc);
  } else {
    // We have no indication of what may be the first admissible arc.
    first_admissible_arc_.Set(node,
                              GetFirstOutgoingOrOppositeIncomingArc(node));
  }
}

template <typename Graph, typename ArcFlowType, typename ArcScaledCostType>
typename Graph::ArcIndex
GenericMinCostFlow<Graph, ArcFlowType, ArcScaledCostType>::Opposite(
    ArcIndex arc) const {
  return Graphs<Graph>::OppositeArc(*graph_, arc);
}

template <typename Graph, typename ArcFlowType, typename ArcScaledCostType>
bool GenericMinCostFlow<Graph, ArcFlowType, ArcScaledCostType>::IsArcValid(
    ArcIndex arc) const {
  return Graphs<Graph>::IsArcValid(*graph_, arc);
}

template <typename Graph, typename ArcFlowType, typename ArcScaledCostType>
bool GenericMinCostFlow<Graph, ArcFlowType, ArcScaledCostType>::IsArcDirect(
    ArcIndex arc) const {
  assert(IsArcValid(arc));
  return arc >= 0;
}

// Explicit instantiations that can be used by a client.
//
// TODO(user): Move this code out of a .cc file and include it at the end of
// the header so it can work with any graph implementation?
template class GenericMinCostFlow<StarGraph>;
template class GenericMinCostFlow<::util::ReverseArcListGraph<>>;
template class GenericMinCostFlow<::util::ReverseArcStaticGraph<>>;
template class GenericMinCostFlow<::util::ReverseArcMixedGraph<>>;
template class GenericMinCostFlow<
    ::util::ReverseArcStaticGraph<uint16_t, int32_t>>;

// A more memory-efficient version for large graphs.
template class GenericMinCostFlow<
    ::util::ReverseArcStaticGraph<uint16_t, int32_t>,
    /*ArcFlowType=*/int16_t,
    /*ArcScaledCostType=*/int32_t>;

SimpleMinCostFlow::SimpleMinCostFlow(NodeIndex reserve_num_nodes,
                                     ArcIndex reserve_num_arcs) {
  if (reserve_num_nodes > 0) {
    node_supply_.reserve(reserve_num_nodes);
  }
  if (reserve_num_arcs > 0) {
    arc_tail_.reserve(reserve_num_arcs);
    arc_head_.reserve(reserve_num_arcs);
    arc_capacity_.reserve(reserve_num_arcs);
    arc_cost_.reserve(reserve_num_arcs);
    arc_permutation_.reserve(reserve_num_arcs);
    arc_flow_.reserve(reserve_num_arcs);
  }
}

void SimpleMinCostFlow::SetNodeSupply(NodeIndex node, FlowQuantity supply) {
  ResizeNodeVectors(node);
  node_supply_[node] = supply;
}

ArcIndex SimpleMinCostFlow::AddArcWithCapacityAndUnitCost(NodeIndex tail,
                                                          NodeIndex head,
                                                          FlowQuantity capacity,
                                                          CostValue unit_cost) {
  ResizeNodeVectors(std::max(tail, head));
  const ArcIndex arc = arc_tail_.size();
  arc_tail_.push_back(tail);
  arc_head_.push_back(head);
  arc_capacity_.push_back(capacity);
  arc_cost_.push_back(unit_cost);
  return arc;
}

ArcIndex SimpleMinCostFlow::PermutedArc(ArcIndex arc) {
  return arc < arc_permutation_.size() ? arc_permutation_[arc] : arc;
}

SimpleMinCostFlow::Status SimpleMinCostFlow::SolveWithPossibleAdjustment(
    SupplyAdjustment adjustment) {
  optimal_cost_ = 0;
  maximum_flow_ = 0;
  arc_flow_.clear();
  const NodeIndex num_nodes = node_supply_.size();
  const ArcIndex num_arcs = arc_capacity_.size();
  if (num_nodes == 0) return OPTIMAL;

  int supply_node_count = 0, demand_node_count = 0;
  FlowQuantity total_supply = 0, total_demand = 0;
  for (NodeIndex node = 0; node < num_nodes; ++node) {
    if (node_supply_[node] > 0) {
      ++supply_node_count;
      total_supply += node_supply_[node];
    } else if (node_supply_[node] < 0) {
      ++demand_node_count;
      total_demand -= node_supply_[node];
    }
  }
  if (adjustment == DONT_ADJUST && total_supply != total_demand) {
    return UNBALANCED;
  }

  // Feasibility checking, and possible supply/demand adjustment, is done by:
  // 1. Creating a new source and sink node.
  // 2. Taking all nodes that have a non-zero supply or demand and
  //    connecting them to the source or sink respectively. The arc thus
  //    added has a capacity of the supply or demand.
  // 3. Computing the max flow between the new source and sink.
  // 4. If adjustment isn't being done, checking that the max flow is equal
  //    to the total supply/demand (and returning INFEASIBLE if it isn't).
  // 5. Running min-cost max-flow on this augmented graph, using the max
  //    flow computed in step 3 as the supply of the source and demand of
  //    the sink.
  const ArcIndex augmented_num_arcs =
      num_arcs + supply_node_count + demand_node_count;
  const NodeIndex source = num_nodes;
  const NodeIndex sink = num_nodes + 1;
  const NodeIndex augmented_num_nodes = num_nodes + 2;

  Graph graph(augmented_num_nodes, augmented_num_arcs);
  for (ArcIndex arc = 0; arc < num_arcs; ++arc) {
    graph.AddArc(arc_tail_[arc], arc_head_[arc]);
  }

  for (NodeIndex node = 0; node < num_nodes; ++node) {
    if (node_supply_[node] > 0) {
      graph.AddArc(source, node);
    } else if (node_supply_[node] < 0) {
      graph.AddArc(node, sink);
    }
  }

  graph.Build(&arc_permutation_);

  {
    GenericMaxFlow<Graph> max_flow(&graph, source, sink);
    ArcIndex arc;
    for (arc = 0; arc < num_arcs; ++arc) {
      max_flow.SetArcCapacity(PermutedArc(arc), arc_capacity_[arc]);
    }
    for (NodeIndex node = 0; node < num_nodes; ++node) {
      if (node_supply_[node] != 0) {
        max_flow.SetArcCapacity(PermutedArc(arc), std::abs(node_supply_[node]));
        ++arc;
      }
    }
    if (arc != augmented_num_arcs) {
      throw isce3::except::RuntimeError(
        ISCE_SRCINFO(), "arc != augmented_num_arcs");
    }
    if (!max_flow.Solve()) {
      pyre::journal::info_t channel("isce3.unwrap.ortools.min_cost_flow");
      channel << pyre::journal::at(__HERE__)
              << "Max flow could not be computed."
              << pyre::journal::endl;
      switch (max_flow.status()) {
        case MaxFlowStatusClass::NOT_SOLVED:
          return NOT_SOLVED;
        case MaxFlowStatusClass::OPTIMAL:
          channel << pyre::journal::at(__HERE__)
                  << "Max flow failed but claimed to have an optimal solution"
                  << pyre::journal::endl;
          [[fallthrough]];
        default:
          return BAD_RESULT;
      }
    }
    maximum_flow_ = max_flow.GetOptimalFlow();
  }

  if (adjustment == DONT_ADJUST && maximum_flow_ != total_supply) {
    return INFEASIBLE;
  }

  GenericMinCostFlow<Graph> min_cost_flow(&graph);
  ArcIndex arc;
  for (arc = 0; arc < num_arcs; ++arc) {
    ArcIndex permuted_arc = PermutedArc(arc);
    min_cost_flow.SetArcUnitCost(permuted_arc, arc_cost_[arc]);
    min_cost_flow.SetArcCapacity(permuted_arc, arc_capacity_[arc]);
  }
  for (NodeIndex node = 0; node < num_nodes; ++node) {
    if (node_supply_[node] != 0) {
      ArcIndex permuted_arc = PermutedArc(arc);
      min_cost_flow.SetArcCapacity(permuted_arc, std::abs(node_supply_[node]));
      min_cost_flow.SetArcUnitCost(permuted_arc, 0);
      ++arc;
    }
  }
  min_cost_flow.SetNodeSupply(source, maximum_flow_);
  min_cost_flow.SetNodeSupply(sink, -maximum_flow_);
  min_cost_flow.SetCheckFeasibility(false);

  arc_flow_.resize(num_arcs);
  if (min_cost_flow.Solve()) {
    optimal_cost_ = min_cost_flow.GetOptimalCost();
    for (arc = 0; arc < num_arcs; ++arc) {
      arc_flow_[arc] = min_cost_flow.Flow(PermutedArc(arc));
    }
  }
  return min_cost_flow.status();
}

CostValue SimpleMinCostFlow::OptimalCost() const { return optimal_cost_; }

FlowQuantity SimpleMinCostFlow::MaximumFlow() const { return maximum_flow_; }

FlowQuantity SimpleMinCostFlow::Flow(ArcIndex arc) const {
  return arc_flow_[arc];
}

NodeIndex SimpleMinCostFlow::NumNodes() const { return node_supply_.size(); }

ArcIndex SimpleMinCostFlow::NumArcs() const { return arc_tail_.size(); }

ArcIndex SimpleMinCostFlow::Tail(ArcIndex arc) const { return arc_tail_[arc]; }

ArcIndex SimpleMinCostFlow::Head(ArcIndex arc) const { return arc_head_[arc]; }

FlowQuantity SimpleMinCostFlow::Capacity(ArcIndex arc) const {
  return arc_capacity_[arc];
}

CostValue SimpleMinCostFlow::UnitCost(ArcIndex arc) const {
  return arc_cost_[arc];
}

FlowQuantity SimpleMinCostFlow::Supply(NodeIndex node) const {
  return node_supply_[node];
}

void SimpleMinCostFlow::ResizeNodeVectors(NodeIndex node) {
  if (node < node_supply_.size()) return;
  node_supply_.resize(node + 1);
}

}  // namespace operations_research

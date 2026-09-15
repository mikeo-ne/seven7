#pragma once
// seven7 — Engine layer: DSP graph (spec: docs/01 ARC-C03; docs/03 MIX-05, MIX-07)
//
// Pull-based processing graph skeleton:
//   - Nodes carry reported latency (plug-ins, resamplers, HW inserts).
//   - Routing commits are validated: cycles are never admitted (MIX-05).
//   - Delay compensation (MIX-07) aligns every path into a junction by inserting
//     per-edge delay lines:  inserted(u→v) = max_pred_arrival(v) − arrival(u).

#include <cstdint>
#include <string>
#include <vector>

namespace s7::engine {

using NodeId = std::uint32_t;

struct Node {
  NodeId id = 0;
  std::string name;
  std::int64_t latency_samples = 0; // as reported at instantiate / on change
  double gain = 1.0;                // static staged gain for the headless render path
};

struct EdgeDelay {
  NodeId from;
  NodeId to;
  std::int64_t inserted_samples;
};

/// Result of a MIX-07 compensation pass toward one summing junction.
struct CompensationReport {
  std::int64_t domain_delay = 0;         // what every path into the junction is aligned to
  std::vector<EdgeDelay> inserted;       // per-edge alignment delays
  std::vector<NodeId> over_cap;          // strips whose arrival exceeds the cap (red badge)
};

class DspGraph {
public:
  NodeId add_node(std::string name, std::int64_t latency_samples = 0, double gain = 1.0);

  /// MIX-05 commit rule: returns false (and changes nothing) if the edge would
  /// close a cycle. Identity/self edges are rejected as well.
  bool add_edge(NodeId from, NodeId to);

  /// Kahn topological order over the whole graph. Returns false if cyclic
  /// (defensive — add_edge already prevents cycles; used by CI audits).
  bool topo_order(std::vector<NodeId>& out) const;

  /// MIX-07: compute alignment for all paths converging on `target`.
  /// arrival(n) = latency(n) + max over predecessors of arrival(pred).
  CompensationReport compensate(NodeId target, std::int64_t cap) const;

  const Node& node(NodeId id) const { return nodes_.at(id); }
  std::size_t size() const { return nodes_.size(); }

private:
  bool reachable(NodeId from, NodeId to) const; // DFS over out-edges
  void upstream_of(NodeId target, std::vector<bool>& mark) const;

  std::vector<Node> nodes_;
  std::vector<std::vector<NodeId>> preds_; // in-edges per node
  std::vector<std::vector<NodeId>> succs_; // out-edges per node
};

} // namespace s7::engine

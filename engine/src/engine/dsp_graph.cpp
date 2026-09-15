#include "engine/dsp_graph.h"

#include <algorithm>
#include <numeric>
#include <queue>
#include <stdexcept>

namespace s7::engine {

NodeId DspGraph::add_node(std::string name, std::int64_t latency_samples, double gain) {
  const NodeId id = static_cast<NodeId>(nodes_.size());
  nodes_.push_back({id, std::move(name), latency_samples, gain});
  preds_.emplace_back();
  succs_.emplace_back();
  return id;
}

bool DspGraph::reachable(NodeId from, NodeId to) const {
  if (from == to) return true;
  std::vector<bool> seen(nodes_.size(), false);
  std::vector<NodeId> stack{from};
  while (!stack.empty()) {
    const NodeId n = stack.back();
    stack.pop_back();
    if (seen[n]) continue;
    seen[n] = true;
    for (const NodeId m : succs_[n]) {
      if (m == to) return true;
      stack.push_back(m);
    }
  }
  return false;
}

bool DspGraph::add_edge(NodeId from, NodeId to) {
  if (from >= nodes_.size() || to >= nodes_.size())
    throw std::out_of_range("DspGraph::add_edge: unknown node");
  if (from == to) return false;
  // MIX-05: reject anything that would route a destination back into a source.
  if (reachable(to, from)) return false;
  preds_[to].push_back(from);
  succs_[from].push_back(to);
  return true;
}

bool DspGraph::topo_order(std::vector<NodeId>& out) const {
  std::vector<std::size_t> indeg(nodes_.size());
  for (std::size_t i = 0; i < nodes_.size(); ++i) indeg[i] = preds_[i].size();

  std::queue<NodeId> ready;
  for (std::size_t i = 0; i < nodes_.size(); ++i)
    if (indeg[i] == 0) ready.push(static_cast<NodeId>(i));

  out.clear();
  out.reserve(nodes_.size());
  while (!ready.empty()) {
    const NodeId n = ready.front();
    ready.pop();
    out.push_back(n);
    for (const NodeId m : succs_[n])
      if (--indeg[m] == 0) ready.push(m);
  }
  return out.size() == nodes_.size();
}

void DspGraph::upstream_of(NodeId target, std::vector<bool>& mark) const {
  mark.assign(nodes_.size(), false);
  std::vector<NodeId> stack{target};
  while (!stack.empty()) {
    const NodeId n = stack.back();
    stack.pop_back();
    if (mark[n]) continue;
    mark[n] = true;
    for (const NodeId p : preds_[n]) stack.push_back(p);
  }
}

CompensationReport DspGraph::compensate(NodeId target, std::int64_t cap) const {
  if (target >= nodes_.size())
    throw std::out_of_range("DspGraph::compensate: unknown node");

  std::vector<bool> in_scope;
  upstream_of(target, in_scope);

  // Process the upstream subgraph in dependency order; arrival = own latency +
  // max predecessor arrival (longest-path accumulation, MIX-07 §4.1).
  std::vector<std::int64_t> arrival(nodes_.size(), 0);
  // Simple relaxation is fine at skeleton scale: iterate until stable.
  bool changed = true;
  while (changed) {
    changed = false;
    for (std::size_t n = 0; n < nodes_.size(); ++n) {
      if (!in_scope[n]) continue;
      std::int64_t best = 0;
      for (const NodeId p : preds_[n]) {
        if (!in_scope[p]) continue;
        best = std::max(best, arrival[p]);
      }
      const std::int64_t want = nodes_[n].latency_samples + best;
      if (want != arrival[n]) {
        arrival[n] = want;
        changed = true;
      }
    }
  }

  CompensationReport report;
  for (std::size_t v = 0; v < nodes_.size(); ++v) {
    if (!in_scope[v]) continue;
    std::int64_t max_pred = 0;
    for (const NodeId p : preds_[v])
      if (in_scope[p]) max_pred = std::max(max_pred, arrival[p]);
    for (const NodeId p : preds_[v]) {
      if (!in_scope[p]) continue;
      report.inserted.push_back({p, static_cast<NodeId>(v), max_pred - arrival[p]});
    }
    if (v == target) report.domain_delay = max_pred;
    if (v != target && arrival[v] > cap) report.over_cap.push_back(static_cast<NodeId>(v));
  }
  return report;
}

} // namespace s7::engine

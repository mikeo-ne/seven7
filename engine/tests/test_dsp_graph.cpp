// seven7 — graph + routing + PDC tests (docs/01 ARC-C03; docs/03 MIX-05, MIX-07)

#include <algorithm>
#include <cstdint>
#include <vector>

#include "engine/dsp_graph.h"
#include "s7_test.h"

using s7::engine::DspGraph;
using s7::engine::NodeId;

namespace {

bool inserted_delay(const s7::engine::CompensationReport& r, NodeId from, NodeId to,
                    std::int64_t expected) {
  for (const auto& e : r.inserted)
    if (e.from == from && e.to == to) return e.inserted_samples == expected;
  return false;
}

bool contains(const std::vector<NodeId>& v, NodeId id) {
  return std::find(v.begin(), v.end(), id) != v.end();
}

void topo_and_cycles() {
  DspGraph g;
  const NodeId a = g.add_node("A");
  const NodeId b = g.add_node("B");
  const NodeId c = g.add_node("C");
  S7_CHECK(g.add_edge(a, b));
  S7_CHECK(g.add_edge(a, c));
  S7_CHECK(g.add_edge(b, c));

  std::vector<NodeId> order;
  S7_CHECK(g.topo_order(order));
  S7_CHECK(order.size() == 3);
  auto pos = [&](NodeId id) {
    return std::find(order.begin(), order.end(), id) - order.begin();
  };
  S7_CHECK(pos(a) < pos(b));
  S7_CHECK(pos(b) < pos(c));

  // MIX-05: routes that would close a cycle are rejected and change nothing.
  S7_CHECK(!g.add_edge(c, a));
  S7_CHECK(!g.add_edge(c, c));
  S7_CHECK(g.topo_order(order) && order.size() == 3);
}

void delay_compensation() {
  // Fixture: drums go straight to the mix; keys pass a linear-phase EQ (1152 spl).
  DspGraph g;
  const NodeId drums = g.add_node("Drums", 0);
  const NodeId keys = g.add_node("Keys", 0);
  const NodeId eq = g.add_node("LinearEQ(insert)", 1152);
  const NodeId mix = g.add_node("MixBus");
  g.add_edge(drums, mix);
  g.add_edge(keys, eq);
  g.add_edge(eq, mix);

  const auto report = g.compensate(mix, 32768);
  S7_CHECK(report.domain_delay == 1152);
  S7_CHECK(inserted_delay(report, drums, mix, 1152)); // dry path aligned to the EQ path
  S7_CHECK(inserted_delay(report, eq, mix, 0));
  S7_CHECK(report.over_cap.empty());

  // MIX-07 red badge: with a 256-sample cap, the EQ strip reports over-cap.
  const auto capped = g.compensate(mix, 256);
  S7_CHECK(contains(capped.over_cap, eq));
  S7_CHECK(!contains(capped.over_cap, drums));

  // Cross-path alignment: a 9000-spl latency upstream of the junction must be
  // compensated — the dry tap is delayed to meet it (this IS the MIX-07 contract).
  // Independent (disconnected) subtrees must NOT inflate this junction's domain.
  DspGraph g2;
  const NodeId src = g2.add_node("RemoteSource", 9000);
  const NodeId aux = g2.add_node("RemoteAux");
  const NodeId out = g2.add_node("Out");
  const NodeId dry = g2.add_node("DryTap", 0);
  const NodeId far_send = g2.add_node("UnrelatedSend", 99999); // never reaches `out`
  const NodeId elsewhere = g2.add_node("Elsewhere");
  g2.add_edge(src, aux);
  g2.add_edge(aux, out);
  g2.add_edge(dry, out);
  g2.add_edge(far_send, elsewhere);

  const auto r2 = g2.compensate(out, 32768);
  S7_CHECK(r2.domain_delay == 9000);                // upstream latency aligns the junction…
  S7_CHECK(inserted_delay(r2, dry, out, 9000));     // …dry path is delayed to match
  S7_CHECK(inserted_delay(r2, aux, out, 0));        // already the slowest path
  S7_CHECK(inserted_delay(r2, src, aux, 0));        // single-predecessor edge: nothing to align
  for (const auto& e : r2.inserted)                 // disconnected subtree stays out of scope
    S7_CHECK(e.from != far_send && e.to != elsewhere && e.from != elsewhere);

  // Cap bookkeeping on the remote chain.
  const auto r2cap = g2.compensate(out, 5000);
  S7_CHECK(contains(r2cap.over_cap, src));
  S7_CHECK(contains(r2cap.over_cap, aux));
  S7_CHECK(!contains(r2cap.over_cap, dry));
}

} // namespace

int main() {
  topo_and_cycles();
  delay_compensation();
  return s7::test::summary("dsp_graph");
}

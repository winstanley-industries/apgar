# APGAR - A GPU-Accelerated PCB Autorouter Library

## Project Kickoff & Research Foundation (v0.1)

**Status:** Draft
**Purpose:** Establish the architectural direction, research basis, and initial implementation plan for a next-generation GPU-accelerated PCB autorouting library.

---

# 1. Vision

The goal of this project is to build a modern, GPU-native autorouting library intended to become a core component of a future PCB EDA system.

Unlike traditional PCB autorouters, the primary objective is **not simply to route faster**.

Instead, the objective is:

> **Use modern GPU hardware to explore orders of magnitude more routing possibilities, allowing the router to consistently produce higher quality layouts than traditional CPU-based routers.**

GPU acceleration should therefore be viewed as an enabler for substantially larger search spaces rather than merely reducing execution time.

Desired outcomes include:

* Higher routing completion rates
* Lower via count
* Better congestion avoidance
* Better routing topology
* Better support for complex modern boards
* Interactive incremental rerouting
* Future support for signal integrity aware routing

---

# 2. Design Philosophy

Traditional autorouters typically perform:

```
Net
 ↓
Shortest Path
 ↓
Commit
 ↓
Repeat
```

This project instead proposes:

```
Board Geometry
        │
        ▼
Geometry Compiler
        │
        ▼
GPU Candidate Generation
        │
        ▼
Candidate Library
        │
        ▼
Global Allocation
        │
        ▼
Exact Geometry Legalization
        │
        ▼
PCB Output
```

The distinction is important.

Instead of asking:

> "What is the best path for this net?"

the system asks:

> "Given many good paths for every net, what combination produces the best entire board?"

This shifts optimization from individual shortest paths to board-wide optimization.

---

# 3. Guiding Principles

The following principles should remain true throughout development.

## GPU-first

Algorithms should be designed for massive parallelism rather than ported from CPU implementations.

## Exact Geometry

Raster representations are implementation details.

The source of truth is always exact CAD geometry.

## Candidate-Based Routing

Routing candidates are reusable assets.

Generating candidates is expensive.

Selecting between candidates should be comparatively inexpensive.

## Incremental

Small board edits should require only local recomputation.

## Deterministic

Given:

* identical board
* identical seed
* identical settings

the router should produce identical results.

---

# 4. Research Summary

## 4.1 GAMER

The most influential work discovered so far is:

**GPU Accelerated Maze Routing (GAMER)**

Rather than using traditional Dijkstra expansion, GAMER reformulates maze routing as alternating horizontal and vertical sweep operations.

The important insight is that many shortest-path relaxations become combinations of:

* prefix sums
* prefix minimum scans

which execute extremely efficiently on GPUs.

Reported results include:

* ~20× acceleration for coarse maze routing
* ~2.6× acceleration for fine routing
* ~2.7× overall acceleration

without degrading routing quality.

### Applicability

GAMER is directly relevant because:

* scan operations map extremely well to CUDA
* shared-memory kernels are efficient
* routing cost propagation becomes highly parallel

However, GAMER assumes VLSI routing where:

* layers have preferred directions
* changing direction usually implies a via

PCB routing differs substantially:

* turns are inexpensive
* layers support routing in multiple directions
* arbitrary geometry dominates
* many bends may exist on a good path

Therefore:

**GAMER should become one routing kernel—not the entire routing architecture.**

---

## 4.2 InstantGR

InstantGR introduces another important idea.

Instead of continually recomputing routes, it stores alternative routing possibilities inside routing DAGs.

Advantages include:

* reusable candidate representations
* GPU-friendly global optimization
* explicit congestion reasoning
* reduced redundant search

This closely aligns with the desired architecture.

Rather than continually re-running shortest-path searches, we should reuse existing candidate routes whenever possible.

---

## 4.3 Recent PCB Routing Research

Recent PCB routing research has shifted away from purely uniform grids.

Relevant work includes:

* adaptive exploration points
* sparse routing graphs
* polygon partitioning
* dynamic routing regions

The common trend is:

> Use dense search only where geometry requires it.

This strongly suggests that a hybrid representation will outperform a monolithic raster.

---

## 4.4 GPU DRC

Recent GPU DRC work demonstrates that geometric verification itself can also be accelerated.

This suggests the future architecture should treat DRC as another GPU workload rather than a purely CPU post-process.

---

# 5. Architectural Proposal

The proposed architecture consists of six major stages.

## Stage 1 — Exact Board Representation

The canonical board model should contain:

* polygons
* pads
* vias
* tracks
* keepouts
* copper pours
* layer stack
* rules
* net classes

This representation must remain exact.

---

## Stage 2 — Geometry Compiler

The geometry compiler converts exact geometry into GPU-friendly data.

Outputs may include:

* tiled occupancy masks
* edge legality masks
* distance fields
* portal graphs
* congestion fields
* via feasibility maps

This stage should be incremental.

Small board edits should invalidate only affected tiles.

---

## Stage 3 — Candidate Generation

Multiple routing engines should coexist.

Potential engines include:

* GPU sweep routing (GAMER-inspired)
* Sparse frontier routing
* Adaptive exploration routing
* Topological routing
* Escape routing
* Differential pair routing

Every engine should emit a common candidate format.

---

## Stage 4 — Candidate Library

Each net owns many candidates.

A candidate contains:

* topology
* geometry
* resource usage
* cost metrics
* layer usage
* via information
* routing statistics

Candidate diversity is more valuable than candidate quantity.

Future work should measure candidate similarity to avoid redundant storage.

---

## Stage 5 — Global Allocation

This is expected to become the heart of the router.

Rather than repeatedly routing individual nets, the allocator selects one candidate for every net.

It then:

* measures congestion
* updates resource pricing
* regenerates only candidates affected by congestion

This resembles PathFinder but operates over reusable candidates.

---

## Stage 6 — Legalization

Only selected candidates proceed to exact geometry.

Possible legalization operations include:

* shove
* jog insertion
* via relocation
* neck-down
* corner smoothing
* exact DRC

The CAD engine remains the final verification authority.

---

# 6. Representation Strategy

The project should maintain two independent representations.

## Exact Representation

Authoritative geometry.

Never rasterized.

## Search Representation

GPU optimized.

Conservative approximation.

The search representation should never permit geometry that is illegal in the exact model.

False positives are acceptable.

False negatives are not.

---

# 7. Candidate-Based Optimization

This project proposes that routing should become an optimization problem over candidates rather than shortest paths.

For every net:

```
Candidate A
Candidate B
Candidate C
Candidate D
...
```

The allocator chooses exactly one.

Congestion causes prices to increase.

New candidates are generated only where required.

This resembles column generation in mathematical optimization.

---

# 8. Initial Scope

Version 1 should intentionally remain limited.

Supported:

* single-ended nets
* through-hole vias
* H/V/45° routing
* exact DRC
* incremental updates
* multiple routing kernels

Deferred:

* differential pairs
* buses
* impedance control
* tuning
* copper pours
* SI optimization

---

# 9. Open Research Questions

Several architectural questions remain unresolved.

## Rasterization

Should search occur on:

* fixed raster
* adaptive raster
* sparse tiles
* navigation meshes
* hybrid representations

---

## Candidate Diversity

How should diversity be measured?

Potential metrics include:

* overlap
* homotopy class
* resource similarity
* Fréchet distance

---

## Global Optimization

Is PathFinder sufficient?

Should Lagrangian relaxation or other optimization techniques replace negotiation?

---

## GPU Routing Kernels

When should the dispatcher choose:

* sweep routing
* sparse frontier routing
* adaptive exploration
* topology-first routing

---

## Via Modeling

Should vias be:

* graph edges
* templates
* sparse transition portals

---

# 10. Proposed Software Architecture

```
board_ir/
geometry/
geometry_compiler/
gpu_runtime/
route_generators/
candidate_store/
global_allocator/
legalizer/
drc/
specialty/
session/
cad_adapters/
```

Each subsystem should remain independently testable.

---

# 11. First Milestone

The first milestone is intentionally modest.

Build a system capable of:

* importing an exact board
* compiling GPU search fields
* generating multiple legal route candidates
* validating those candidates against exact geometry
* benchmarking GPU vs CPU routing

Success is **not** routing an entire PCB.

Success is validating that the architectural foundation is correct.

---

# 12. Long-Term Vision

The eventual system should become more than an autorouter.

It should become a GPU-native routing platform supporting:

* autorouting
* interactive routing
* incremental rerouting
* escape planning
* bus planning
* differential pairs
* SI-aware routing
* manufacturing optimization

The routing engine should become a reusable library independent of any specific PCB editor.

---

# References

## GPU Routing

**Lin, S., et al.** *GAMER: GPU Accelerated Maze Routing for VLSI Global Routing.* IEEE Transactions on Computer-Aided Design, 2023.
https://appsrv.cse.cuhk.edu.hk/~sjlin/GAMER%20TCAD2023.pdf

**XPlace GPU Global Router (reference implementation)**
https://github.com/cuhk-eda/Xplace

---

## Candidate-Based Global Routing

**Lin, S., et al.** *InstantGR: GPU-Accelerated Global Routing with Route Graphs.*
https://shijulin.github.io/files/1239_Final_Manuscript.pdf

---

## PCB Routing

**Benchoff, B.** *OrthoRoute – GPU Accelerated Autorouting for KiCad.*
https://github.com/bbenchoff/OrthoRoute

---

## PCB Routing Research

**MegaRoute: Universal Automated Large-Scale PCB Routing Method with Adaptive Step-Size Search.**
https://www.researchgate.net/publication/391975742_MegaRoute_Universal_Automated_Large-Scale_PCB_Routing_Method_with_Adaptive_Step-Size_Search

**3D LineExplore: Adaptive Exploration-Based PCB Routing.** Scientific Reports (2026).
https://doi.org/10.1038/s41598-026-36925-0

**Pad-Focused PCB Routing Using Dynamic Partitioning and MCTS.**
https://doaj.org/article/00bd268bd38b4d28976bdafd8c175713

---

## GPU DRC

**OpenDRC: An Efficient Open-Source Design Rule Checking Engine with GPU Acceleration.**
https://research.cuhk.edu.hk/en/publications/opendrc-an-efficient-open-source-design-rule-checking-engine-with-2/

---

## Distance Fields

**Parallel Banding Algorithm (PBA) for Exact Euclidean Distance Transform on GPUs.**
https://www.comp.nus.edu.sg/~tants/pba.html

---

## Evaluation

**PCBWorld: A Real-World Benchmark for PCB Routing.**
https://arxiv.org/abs/2607.05915


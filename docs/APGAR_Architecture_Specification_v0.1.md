# APGAR Architecture Specification v0.1

**A Pretty Good Auto Router**  
**Status:** Draft / living specification  
**Date:** July 17, 2026

This Markdown companion is intentionally abbreviated. The DOCX is the authoritative formatted version of this draft.

## Core thesis

APGAR is a CAD-neutral autorouting library designed to turn GPU throughput into higher-quality PCB layouts. Exact PCB geometry remains authoritative; GPU representations are disposable compiled artifacts. The GPU generates diverse route candidates, negotiates board-wide allocation, and accelerates analysis. Exact legalization and host-CAD validation gate every commit.

## Architecture

```text
CAD Adapter -> Exact Board IR -> Geometry Compiler
                              |-> Sparse tile atlas
                              |-> Directional edge masks
                              |-> Distance / margin fields
                              |-> Topological portals
                              |-> Via-template feasibility

Compiled Board + Routing Request
        -> Candidate Generators
        -> Candidate Store
        -> Global Allocator / Negotiated Prices
        -> Exact Legalizer + Incremental DRC
        -> Host CAD Validation + Commit
```

## Principal decisions

1. Exact geometry is authoritative.
2. Candidate generation is separated from global allocation.
3. Regular grid adjacency is procedural, not stored as a full CSR graph.
4. The implementation is C++ with a CUDA-first backend.
5. Global coordinates use signed 64-bit integer database units.
6. Routing uses hybrid raster and topological representations.
7. The host CAD engine is the final validation authority.

## First milestone

Import a small board into Exact Board IR; compile conservative sparse H/V/45-degree fields; generate multiple candidates with CPU A* and one GPU kernel; validate every candidate exactly; emit a reproducible benchmark and replay bundle.

## Full specification

See `APGAR_Architecture_Specification_v0.1.docx` for the complete requirements, data models, APIs, GPU execution model, algorithms, testing strategy, roadmap, risks, and references.

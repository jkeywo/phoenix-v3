# Portable foundation contracts

Read `data/` for JSON/bytes, hashing and bounded IO; `contracts/` for shared IDs, commands and permitted observations; `input/` for logical route/device input; then `console-controls/` for advisory command eligibility. Their owners are phoenix_data, phoenix_contracts, phoenix_input and phoenix_console_controls respectively.

This family sits below content, native gameplay, sessions and rendering. It supplies values and checks, not a World, catalogue resolver, device backend or command authority. The remaining phoenix_foundation link convenience target groups the leaves; declarations still have one named-module owner.

Input/semantics, content, storage and network integration cases exercise these contracts. Follow the central migration receipts for per-platform evidence and preserve public defaults/serialization/identity rules when changing the foundation.

# Copilot Engineering Ruleset (Strict)

## 1. Code Style Requirements

- Follow existing project patterns without deviation.
- Use explicit types everywhere; no implicit inference for public APIs.
- Prefer pure functions; avoid side effects unless explicitly required.
- Enforce immutability for all data structures unless mutation is part of the design.
- No magic numbers; define constants in a dedicated module.
- Use descriptive, unambiguous names for variables, functions, and modules.

## 2. Architectural Constraints

- Do not introduce new dependencies without explicit instruction.
- Maintain current folder and module structure; do not reorganize files.
- All new modules must follow the established layering (e.g., `api → service → data`).
- Never bypass existing abstractions or interfaces.
- Avoid global state entirely unless the project already uses global state; use dependency injection where applicable.

## 3. Documentation & Comments

- Every function, class, and module must include a docstring describing purpose, inputs, outputs, and failure modes.
- Inline comments only for non-obvious logic; do not restate what the code already expresses.
- Document assumptions explicitly.
- When generating code, include a short “Design Rationale” comment block.

## 4. Testing Requirements

- All generated code must include corresponding unit tests.
- Tests must follow the existing test framework and naming conventions.
- Cover edge cases, error conditions, and boundary values.
- No mocking of internal logic; only mock external systems.

## 5. Security & Reliability Rules

- Validate all inputs, even internal ones.
- Never log sensitive data.
- Use constant-time comparisons for security‑sensitive operations.
- Handle all error conditions explicitly; no silent failures.
- Prefer fail‑fast behavior with clear error messages.

## 6. Performance Constraints

- Avoid unnecessary allocations, copies, or deep clones.
- Use streaming or chunked processing for large data.
- Prefer O(n) solutions; avoid O(n²) unless explicitly justified.
- Do not introduce blocking operations in async workflows.

## 7. Output Behavior for Copilot

- When asked for code, produce complete, runnable modules.
- When asked for explanations, be concise and technical.
- When uncertain, ask clarifying questions instead of guessing.
- Never generate placeholder logic, TODOs, or speculative APIs.

## 8. Prohibited Actions

- Do not modify configuration files unless explicitly instructed.
- Do not generate business logic without requirements.
- Do not introduce stylistic changes or refactors unless requested.
- Do not produce pseudocode unless explicitly asked.

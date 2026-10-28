# Bytes, JSON and bounded file operations

`data.cppm` exports `phoenix.data`; link `phoenix_data`. It owns Json/WireJson and Bytes, SHA-256, bounded file reads, identifier/token validation and checked JSON scalars. `atomic_file.cpp` implements staged replacement/commit; `random.cpp` owns secure token generation; `data.cpp` contains the portable data operations.

This is a low-level owner with no catalogue, gameplay, GUI or authoritative session. Authored identifiers and protocol/storage tokens deliberately use separate validation rules. StagedFileReplacement owns its temporary state and commits explicitly; higher storage/content adapters choose when replacement is appropriate.

Content, persistence and network cases cover bounds, identities, failed replacement and malformed scalar input. Preserve unsigned range checks before narrowing and caller-provided default semantics. Migration receipts record actual execution and OS coverage; merely compiling the IO abstraction is not an OS durability test.

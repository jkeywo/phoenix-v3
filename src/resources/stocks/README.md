# Stock definitions and transactions

`stocks.cppm` exports `phoenix.resources.stocks`. Link `phoenix_resources_stocks_definitions` for immutable resource/store/filter/recipe contracts and `phoenix_resources_stocks` for StockStore and transaction execution. `stock_definitions.cpp` validates generic definitions; `stock.cpp` owns finite inventory, selection, reservations and atomic transfer/accounting.

This is a foundation-level resource mechanism, with no catalogue lookup, equipment or World dependency. Content compilers resolve authored resource IDs; fitted adapters supply concrete stores and funded work. Capacity sizes, reservation identities and selection revisions remain part of the public accounting contract.

Existing stock-accounting, processor, cargo and allocation cases verify refusal/atomicity and continuation without freezing shipped tuning. Per-revision native/browser evidence lives in the migration receipt; a definition target build alone does not exercise transactions.

# Gameplay vocabulary

Start with `gameplay_vocabulary.cppm` (`phoenix.gameplay_vocabulary`). Its implementation owns stable observation/action descriptors and registries shared by authored gameplay trees.

`phoenix_behavior_vocabulary` links only foundation and state-tree definitions. Filtering a permitted vocabulary changes availability, not stable numeric indices. This module does not own Controller instance memory, command admission, observations or physical execution.

Existing content binding, summary and gameplay foundation cases verify typed registry use. Index layouts are compatibility contracts; moving this code must not reorder them.

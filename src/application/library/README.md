# User library

`phoenix.menu` owns the installed pack index, managed saves, launch configuration and persistent user preferences. Its files live in the user data directory rather than the launcher's working directory.

It consumes content archives through the production pack and catalogue interfaces. Archive validation, ordering, hashes and save formats retain their existing owners. Session and renderer code do not belong here.

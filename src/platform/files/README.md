# User file and clipboard services

Start at `files.cppm` (`phoenix.files`).

Owns import/save/download/clipboard request lifecycle and Windows/Linux/browser implementations. It does not resolve gameplay authority; file contents still pass production content/session validation.

Allowed dependencies: Content/data contracts and OS dialog services. User file operations are distinct from low-level blob/journal storage.

Ownership and lifetime: FileImportInbox owns request identity and cancellation/completion. Browser completion waits for its real asynchronous callback, including durable managed-library saves.

Relevant validation: `system.file-imports`; real file dialogs and browser download/drop journeys remain manual. These identify checks to run; this README is not execution evidence.

# User paths

`phoenix.platform.user_paths` is a separate small target in this folder. It discovers the native user data directory from Windows known folders or Linux environment conventions, retaining the existing fallback on other platforms. It has no file-picker or content dependency.

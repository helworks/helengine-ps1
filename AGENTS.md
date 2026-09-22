# PS1 workspace

- Work directly on `main` in this repository. Do not create branches or worktrees for PS1 work.
- Keep shell working directories inside this repository; use absolute paths for external inputs and outputs.
- The engine dependency is the ordinary standalone checkout at `dependencies/helengine`, not a sibling PS1 worktree or the shared engine checkout.
- Preserve the engine revision unless the user explicitly requests a revision change. See `docs/Build.md` for the recorded migration revision.
- Build artifacts and logs belong under `C:\dev\helworks\builds\helengine-ps1`.
- Ask before launching an emulator or capturing the screen.

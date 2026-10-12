Read PORTING.md before modifying this repository.

During the SDL3 phase this repository is read-only.
Do not modify OpenGOAL source until explicitly instructed that the SDL3
hardware milestone is complete.

## Agent handoff workflow

This repository is also reviewed through ChatGPT.

Before beginning each major PS5-port milestone:
- Read CHATGPT_REVIEW.md if it exists.
- Treat newer review notes as architectural review, not permission to violate
  PORTING.md or other project guardrails.

After each meaningful milestone, hardware candidate, or architectural blocker:
- Update AGENT_HANDOFF.md with the current state.
- Keep the handoff concise. Do not paste raw logs or command transcripts.
- Include the current commit, artifact path if one exists, blocker, and exact
  next action.
- Commit and push the checkpoint to origin/feature/ps5-port.

Do not put proprietary game data, credentials, SDK files, or raw console dumps
in either handoff file.

## PS5 hardware automation

The PS5 hardware cycle is proven and available from:

  ../ps5-homebrew-dev-protocol

Environment:
- PS5_HOST is set in the shell.
- PS5_LOCK is set.
- PS5_PAYLOAD_SDK is set.
- ftpsrv is available on port 2121.
- elfldr/controller delivery is available on port 9021.
- ShadowMount Plus mounts native titles from /data/homebrew/<TITLE_ID>.
- klogsrv is not currently required; use PS5_KLOG=0 unless explicitly enabled.

The full verified development cycle is:

1. Build a native title folder.
2. Freeze it with scripts/freeze-candidate.sh.
3. Run scripts/ps5-cycle.sh.
4. Inspect the evidence directory and application logs.
5. Make one bounded change and repeat.

A known-good SDL3 candidate has already completed:
- verified FTP deployment
- 130 second settle check
- automatic launch
- running-state verification
- automatic close
- no new PS5 error record

For OpenGOAL hardware bring-up:
- Prefer PS5_READY_LOG and PS5_READY_PATTERN once the app has deterministic startup logging.
- Preserve every failed run's evidence.
- Never blindly retry after an unexpected console restart or loss of connectivity.
- Do not launch over another running title.
- Keep one changed variable per hardware run.

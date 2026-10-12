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

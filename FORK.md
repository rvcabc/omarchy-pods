# This is a fork

This checkout is <https://github.com/rvcabc/omarchy-pods>, Bryce Corbin's fork
of [thisisgm/omarchy-pods](https://github.com/thisisgm/omarchy-pods). It is the
terminal branch for one box: an AirPods Pro 2 (A2698) on Omarchy, paired with an
iPhone as well. Features land here first and are proven on that hardware.

Two rules from upstream's CONTRIBUTING.md are relaxed in this fork, on purpose:

- **Attribution.** Commits here may carry a `Co-Authored-By` trailer for the
  coding tool that wrote them. Upstream forbids that. Anything offered back to
  thisisgm/omarchy-pods is rewritten without the trailer first, and the pull
  request body carries the reproduction and evidence upstream asks for.
- **Scope.** The panel here grows a settings window and the daemon grows verbs
  for every AirPods setting macOS exposes. Upstream keeps the panel to the Sound
  menu set. Offer upstream the pieces that fit its scope, one concern per PR.

Everything else in CONTRIBUTING.md and AGENTS.md still applies: the daemon owns
the outside world, parsers carry sample-input comments, magic numbers are named,
defects are reproduced before they are fixed, and real MACs or keys never reach
a commit.

`daemon/UPSTREAM.md` records what was pulled from upstream and what diverged.

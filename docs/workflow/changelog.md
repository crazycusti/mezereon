Changelog workflow

- Append entries with `make log MSG="tag: short description"`.
- The helper resolves branch + HEAD and appends `YYYY-MM-DD HH:MM:SS (branch@sha) - message` to `CHANGELOG`.
- Ensure each functional change (code or docs) gets a short note before committing.
- Manual edits are possible, but prefer the helper so timestamps stay consistent.

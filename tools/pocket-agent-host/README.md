# pocket-agent-host

Development sidecar for PocketEngine's AI editor integration. Phase 0 provides
the versioned JSONL Editor Bridge and a deterministic Fake Agent. It does not
connect to model providers or expose game-editing tools yet.

```bash
npm install
npm run build
npm test
```

The PocketEngine editor launches `dist/index.js` with Node. Override discovery
for development with:

```bash
POCKET_AGENT_NODE=/path/to/node
POCKET_AGENT_HOST_ENTRY=/path/to/dist/index.js
```

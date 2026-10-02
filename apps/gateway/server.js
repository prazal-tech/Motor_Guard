/*
 * INFRA-NEX Motor Guard — Edge Gateway Service
 * Express / Node.js lightweight gateway for local telemetry ingestion & proxying.
 */

const express = require('express');
const cors = require('cors');

const app = express();
const PORT = process.env.PORT || 4000;

app.use(cors());
app.use(express.json());

let latestTelemetry = {
  rpm: 0,
  voltage_V: 0,
  current_A: 0,
  temp_body_C: 0,
  temp_bearing_C: 0,
  vib_g: 0,
  healthScore: 100,
  fault: "DISCONNECTED",
  faultLevel: 0,
  relayState: false,
  uptime: 0,
  timestamp: new Date().toISOString()
};

// Ingest telemetry from ESP32 or Gateway Nodes
app.post('/api/ingest', (req, res) => {
  latestTelemetry = {
    ...req.body,
    timestamp: new Date().toISOString()
  };
  console.log(`[GATEWAY INGEST] Health: ${latestTelemetry.healthScore}/100 | Fault: ${latestTelemetry.fault}`);
  return res.json({ status: 'success', received: true });
});

// Serve cached telemetry to external clients
app.get('/api/telemetry', (req, res) => {
  return res.json(latestTelemetry);
});

app.listen(PORT, () => {
  console.log(`🚀 Motor Guard Gateway Service running on port ${PORT}`);
});

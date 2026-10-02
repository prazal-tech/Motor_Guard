import React, { useState, useEffect } from 'react';
import { Activity, Zap, Thermometer, Gauge, Settings, ShieldAlert, Cpu } from 'lucide-react';
import { LineChart, Line, XAxis, YAxis, CartesianGrid, Tooltip, ResponsiveContainer, Area, AreaChart } from 'recharts';

function App() {
  const [telemetry, setTelemetry] = useState({
    rpm: 0,
    voltage_V: 0,
    current_A: 0,
    temp_body_C: 0,
    vib_g: 0,
    healthScore: 100,
    fault: "DISCONNECTED",
    faultLevel: 0,
    relayState: false,
    timestamp: new Date().toISOString()
  });

  const [history, setHistory] = useState([]);
  const [isConnected, setIsConnected] = useState(false);

  useEffect(() => {
    const fetchTelemetry = async () => {
      try {
        const res = await fetch('http://localhost:4000/api/telemetry');
        if (res.ok) {
          const data = await res.json();
          setTelemetry(data);
          setIsConnected(true);
          
          setHistory(prev => {
            const newHist = [...prev, { time: new Date().toLocaleTimeString().slice(0, 8), rpm: data.rpm, temp: data.temp_body_C, current: data.current_A }];
            if (newHist.length > 20) newHist.shift();
            return newHist;
          });
        }
      } catch (err) {
        setIsConnected(false);
        console.error("Failed to fetch telemetry:", err);
      }
    };

    const interval = setInterval(fetchTelemetry, 1000);
    return () => clearInterval(interval);
  }, []);

  const getHealthColor = (score) => {
    if (score > 80) return 'var(--success)';
    if (score > 50) return 'var(--warning)';
    return 'var(--danger)';
  };

  const getHealthGlow = (score) => {
    if (score > 80) return 'box-shadow: 0 0 20px rgba(16, 185, 129, 0.4)';
    if (score > 50) return 'box-shadow: 0 0 20px rgba(245, 158, 11, 0.4)';
    return 'box-shadow: 0 0 30px rgba(239, 68, 68, 0.6)';
  };

  return (
    <div className="dashboard-container">
      <header className="header">
        <div className="header-title">
          <Cpu className="metric-icon" size={32} />
          <span>Motor Guard Dashboard</span>
        </div>
        <div className="connection-status">
          <div className={`status-indicator ${isConnected ? 'status-online animate-glow' : 'status-offline'}`}></div>
          {isConnected ? 'LIVE / CONNECTED' : 'DISCONNECTED'}
        </div>
      </header>

      <div className="main-grid">
        
        {/* Health Score Overview */}
        <div className={`glass-panel health-card ${telemetry.faultLevel > 1 ? 'animate-danger-glow' : ''}`}>
          <h2 style={{fontSize: '1.25rem', color: 'var(--text-secondary)'}}>System Health</h2>
          
          <div className="health-circle" style={{ borderColor: getHealthColor(telemetry.healthScore), boxShadow: isConnected ? `0 0 30px ${getHealthColor(telemetry.healthScore)}40` : 'none' }}>
            <span className="health-value" style={{ color: getHealthColor(telemetry.healthScore) }}>
              {telemetry.healthScore}
            </span>
            <span className="health-label">SCORE</span>
          </div>

          <div 
            className="status-msg" 
            style={{ 
              backgroundColor: telemetry.faultLevel === 0 ? 'rgba(16, 185, 129, 0.1)' : 
                               telemetry.faultLevel === 1 ? 'rgba(245, 158, 11, 0.1)' : 'rgba(239, 68, 68, 0.2)',
              color: telemetry.faultLevel === 0 ? 'var(--success)' : 
                     telemetry.faultLevel === 1 ? 'var(--warning)' : 'var(--danger)',
              border: `1px solid ${telemetry.faultLevel === 0 ? 'var(--success)' : 
                     telemetry.faultLevel === 1 ? 'var(--warning)' : 'var(--danger)'}`
            }}
          >
            {telemetry.faultLevel > 0 && <ShieldAlert size={18} style={{marginRight: '8px', verticalAlign: 'middle'}}/>}
            {telemetry.fault}
          </div>
          
          <div style={{ marginTop: '24px', width: '100%'}}>
            <div style={{ display: 'flex', justifyContent: 'space-between', marginBottom: '8px', fontSize: '0.875rem' }}>
              <span>Motor Power Relay</span>
              <span style={{ color: telemetry.relayState ? 'var(--success)' : 'var(--danger)', fontWeight: 'bold' }}>
                {telemetry.relayState ? 'ENERGIZED' : 'TRIPPED'}
              </span>
            </div>
            <button style={{
              width: '100%', padding: '12px', borderRadius: '8px', border: 'none',
              background: telemetry.relayState ? 'var(--danger)' : 'var(--success)',
              color: '#fff', fontWeight: 'bold', cursor: 'pointer', transition: 'all 0.2s',
              opacity: isConnected ? 1 : 0.5
            }}>
              {telemetry.relayState ? 'MANUAL TRIP' : 'RESET RELAY'}
            </button>
          </div>
        </div>

        <div>
          {/* Metrics Grid */}
          <div className="metrics-grid">
            <div className="glass-panel metric-item">
              <div className="metric-header">
                <span>Rotor Speed</span>
                <Gauge className="metric-icon" size={20} />
              </div>
              <div className="metric-body">
                <span className="metric-value">{telemetry.rpm.toFixed(1)}</span>
                <span className="metric-unit">RPM</span>
              </div>
            </div>

            <div className="glass-panel metric-item">
              <div className="metric-header">
                <span>Load Current</span>
                <Zap className="metric-icon" size={20} />
              </div>
              <div className="metric-body">
                <span className="metric-value">{telemetry.current_A.toFixed(3)}</span>
                <span className="metric-unit">Amps</span>
              </div>
            </div>

            <div className="glass-panel metric-item">
              <div className="metric-header">
                <span>Line Voltage</span>
                <Activity className="metric-icon" size={20} />
              </div>
              <div className="metric-body">
                <span className="metric-value">{telemetry.voltage_V.toFixed(1)}</span>
                <span className="metric-unit">Volts</span>
              </div>
            </div>

            <div className="glass-panel metric-item">
              <div className="metric-header">
                <span>Motor Temp</span>
                <Thermometer className="metric-icon" size={20} />
              </div>
              <div className="metric-body">
                <span className="metric-value">{telemetry.temp_body_C.toFixed(1)}</span>
                <span className="metric-unit">°C</span>
              </div>
            </div>
          </div>

          {/* Chart Section */}
          <div className="glass-panel chart-section" style={{ marginTop: '24px' }}>
            <div className="chart-header">
              <span className="chart-title">Real-time Telemetry Trend</span>
            </div>
            <div style={{ flex: 1, width: '100%', height: '100%' }}>
              <ResponsiveContainer width="100%" height="100%">
                <AreaChart data={history} margin={{ top: 10, right: 10, left: -20, bottom: 0 }}>
                  <defs>
                    <linearGradient id="colorRpm" x1="0" y1="0" x2="0" y2="1">
                      <stop offset="5%" stopColor="#3b82f6" stopOpacity={0.3}/>
                      <stop offset="95%" stopColor="#3b82f6" stopOpacity={0}/>
                    </linearGradient>
                  </defs>
                  <CartesianGrid strokeDasharray="3 3" stroke="var(--surface-border)" vertical={false} />
                  <XAxis dataKey="time" stroke="var(--text-secondary)" fontSize={12} tickLine={false} axisLine={false} />
                  <YAxis stroke="var(--text-secondary)" fontSize={12} tickLine={false} axisLine={false} />
                  <Tooltip 
                    contentStyle={{ backgroundColor: 'var(--surface)', border: '1px solid var(--surface-border)', borderRadius: '8px', color: '#fff' }}
                    itemStyle={{ color: '#fff' }}
                  />
                  <Area type="monotone" dataKey="rpm" stroke="#3b82f6" strokeWidth={3} fillOpacity={1} fill="url(#colorRpm)" isAnimationActive={false} />
                </AreaChart>
              </ResponsiveContainer>
            </div>
          </div>
        </div>
        
      </div>
    </div>
  );
}

export default App;

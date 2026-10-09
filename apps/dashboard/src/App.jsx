import React, { useState, useEffect } from 'react';
import { Activity, Zap, Thermometer, Gauge, Settings, ShieldAlert, Cpu, Waves, Clock, Battery } from 'lucide-react';
import { LineChart, Line, XAxis, YAxis, CartesianGrid, Tooltip, ResponsiveContainer, Area, AreaChart } from 'recharts';
import { createClient } from '@supabase/supabase-js';

// Initialize Supabase (Will be null if keys are not set)
const supabaseUrl = import.meta.env.VITE_SUPABASE_URL;
const supabaseKey = import.meta.env.VITE_SUPABASE_ANON_KEY;
const supabase = (supabaseUrl && supabaseKey) ? createClient(supabaseUrl, supabaseKey) : null;

function App() {
  const [telemetry, setTelemetry] = useState({
    rpm: 0,
    ac_voltage_v: 0,
    bus_voltage_v: 0,
    current_a: 0,
    temp_body_c: 0,
    temp_bearing_c: 0,
    vib_magnitude: 0,
    uptime_seconds: 0,
    health_score: 100,
    fault_msg: "DISCONNECTED",
    fault_level: 0,
    relay_state: false,
    timestamp: new Date().toISOString()
  });

  const [history, setHistory] = useState([]);
  const [isConnected, setIsConnected] = useState(false);

  useEffect(() => {
    let baseUptime = 0;
    
    const generateDummyTelemetry = () => {
      baseUptime += 1;
      const rpmBase = 1450;
      const rpm = rpmBase + (Math.random() * 50 - 25);
      const ac_voltage = 230 + (Math.random() * 4 - 2);
      const bus_voltage = 12 + (Math.random() * 0.2 - 0.1);
      const current = 2.5 + (Math.random() * 0.4 - 0.2);
      const tempBody = 45 + (Math.random() * 2 - 1);
      const tempBearing = tempBody + 2.5 + (Math.random() * 1);
      const vib = 0.5 + (Math.random() * 0.2);
      
      const isFault = tempBody > 65 || current > 4.0;
      
      const newData = {
        rpm: rpm,
        ac_voltage_v: ac_voltage,
        bus_voltage_v: bus_voltage,
        current_a: current,
        temp_body_c: tempBody,
        temp_bearing_c: tempBearing,
        vib_magnitude: vib,
        uptime_seconds: baseUptime,
        health_score: isFault ? 45 : Math.floor(95 + Math.random() * 5),
        fault_msg: isFault ? "WARNING - HIGH LOAD" : "SYSTEM NORMAL",
        fault_level: isFault ? 1 : 0,
        relay_state: true,
        timestamp: new Date().toISOString()
      };
      
      setTelemetry(newData);
      setIsConnected(true);
      
      setHistory(prev => {
        const newHist = [...prev, { 
            time: new Date().toLocaleTimeString().slice(0, 8), 
            rpm: newData.rpm, 
            temp: newData.temp_body_c, 
            current: newData.current_a,
            vib: newData.vib_magnitude
        }];
        if (newHist.length > 20) newHist.shift();
        return newHist;
      });
    };

    // Setup Supabase Realtime if configured
    if (supabase) {
      console.log("Supabase configured! Listening for real-time telemetry...");
      setIsConnected(true);
      
      const fetchInitialData = async () => {
        const { data, error } = await supabase
          .from('telemetry')
          .select('*')
          .order('created_at', { ascending: false })
          .limit(1);
        
        if (data && data.length > 0) {
          updateUIWithData(data[0]);
        }
      };

      fetchInitialData();

      const subscription = supabase
        .channel('telemetry-changes')
        .on('postgres_changes', { event: 'INSERT', schema: 'public', table: 'telemetry' }, payload => {
          updateUIWithData(payload.new);
        })
        .subscribe();

      return () => {
        supabase.removeChannel(subscription);
      };
    } else {
      // Fallback: Generate Dummy Telemetry if no Supabase keys
      console.log("No Supabase keys found in .env. Using simulated data.");
      generateDummyTelemetry();
      const interval = setInterval(generateDummyTelemetry, 1000);
      return () => clearInterval(interval);
    }
  }, []);

  const updateUIWithData = (newData) => {
    setTelemetry(newData);
    setHistory(prev => {
      const newHist = [...prev, { 
          time: new Date(newData.timestamp || Date.now()).toLocaleTimeString().slice(0, 8), 
          rpm: newData.rpm, 
          temp: newData.temp_body_c, 
          current: newData.current_a,
          vib: newData.vib_magnitude
      }];
      if (newHist.length > 20) newHist.shift();
      return newHist;
    });
  };

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

  const formatUptime = (seconds) => {
    const h = Math.floor(seconds / 3600);
    const m = Math.floor((seconds % 3600) / 60);
    const s = Math.floor(seconds % 60);
    return `${h}h ${m}m ${s}s`;
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
        <div className={`glass-panel health-card ${telemetry.fault_level > 1 ? 'animate-danger-glow' : ''}`}>
          <h2 style={{fontSize: '1.25rem', color: 'var(--text-secondary)'}}>System Health</h2>
          
          <div className="health-circle" style={{ borderColor: getHealthColor(telemetry.health_score), boxShadow: isConnected ? `0 0 30px ${getHealthColor(telemetry.health_score)}40` : 'none' }}>
            <span className="health-value" style={{ color: getHealthColor(telemetry.health_score) }}>
              {telemetry.health_score}
            </span>
            <span className="health-label">SCORE</span>
          </div>

          <div 
            className="status-msg" 
            style={{ 
              backgroundColor: telemetry.fault_level === 0 ? 'rgba(16, 185, 129, 0.1)' : 
                               telemetry.fault_level === 1 ? 'rgba(245, 158, 11, 0.1)' : 'rgba(239, 68, 68, 0.2)',
              color: telemetry.fault_level === 0 ? 'var(--success)' : 
                     telemetry.fault_level === 1 ? 'var(--warning)' : 'var(--danger)',
              border: `1px solid ${telemetry.fault_level === 0 ? 'var(--success)' : 
                     telemetry.fault_level === 1 ? 'var(--warning)' : 'var(--danger)'}`
            }}
          >
            {telemetry.fault_level > 0 && <ShieldAlert size={18} style={{marginRight: '8px', verticalAlign: 'middle'}}/>}
            {telemetry.fault_msg}
          </div>
          
          <div style={{ marginTop: '24px', width: '100%'}}>
            <div style={{ display: 'flex', justifyContent: 'space-between', marginBottom: '8px', fontSize: '0.875rem' }}>
              <span>Motor Power Relay</span>
              <span style={{ color: telemetry.relay_state ? 'var(--success)' : 'var(--danger)', fontWeight: 'bold' }}>
                {telemetry.relay_state ? 'ENERGIZED' : 'TRIPPED'}
              </span>
            </div>
            <button style={{
              width: '100%', padding: '12px', borderRadius: '8px', border: 'none',
              background: telemetry.relay_state ? 'var(--danger)' : 'var(--success)',
              color: '#fff', fontWeight: 'bold', cursor: 'pointer', transition: 'all 0.2s',
              opacity: isConnected ? 1 : 0.5
            }}>
              {telemetry.relay_state ? 'MANUAL TRIP' : 'RESET RELAY'}
            </button>
          </div>

          <div style={{ marginTop: '24px', width: '100%', display: 'flex', justifyContent: 'space-between', color: 'var(--text-secondary)', fontSize: '0.875rem' }}>
             <span style={{ display: 'flex', alignItems: 'center', gap: '4px' }}><Clock size={16}/> Uptime</span>
             <span>{formatUptime(telemetry.uptime_seconds)}</span>
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
                <span className="metric-value">{Number(telemetry.rpm).toFixed(1)}</span>
                <span className="metric-unit">RPM</span>
              </div>
            </div>

            <div className="glass-panel metric-item">
              <div className="metric-header">
                <span>Load Current</span>
                <Zap className="metric-icon" size={20} />
              </div>
              <div className="metric-body">
                <span className="metric-value">{Number(telemetry.current_a).toFixed(3)}</span>
                <span className="metric-unit">Amps</span>
              </div>
            </div>

            <div className="glass-panel metric-item">
              <div className="metric-header">
                <span>AC Line Voltage</span>
                <Activity className="metric-icon" size={20} />
              </div>
              <div className="metric-body">
                <span className="metric-value">{Number(telemetry.ac_voltage_v).toFixed(1)}</span>
                <span className="metric-unit">Volts</span>
              </div>
            </div>

            <div className="glass-panel metric-item">
              <div className="metric-header">
                <span>Body Temp</span>
                <Thermometer className="metric-icon" size={20} />
              </div>
              <div className="metric-body">
                <span className="metric-value">{Number(telemetry.temp_body_c).toFixed(1)}</span>
                <span className="metric-unit">°C</span>
              </div>
            </div>

            <div className="glass-panel metric-item">
              <div className="metric-header">
                <span>Bearing Temp</span>
                <Thermometer className="metric-icon" size={20} />
              </div>
              <div className="metric-body">
                <span className="metric-value">{Number(telemetry.temp_bearing_c).toFixed(1)}</span>
                <span className="metric-unit">°C</span>
              </div>
            </div>

            <div className="glass-panel metric-item">
              <div className="metric-header">
                <span>Vibration</span>
                <Waves className="metric-icon" size={20} />
              </div>
              <div className="metric-body">
                <span className="metric-value">{Number(telemetry.vib_magnitude).toFixed(2)}</span>
                <span className="metric-unit">g</span>
              </div>
            </div>

            <div className="glass-panel metric-item">
              <div className="metric-header">
                <span>DC Bus Voltage</span>
                <Battery className="metric-icon" size={20} />
              </div>
              <div className="metric-body">
                <span className="metric-value">{Number(telemetry.bus_voltage_v).toFixed(2)}</span>
                <span className="metric-unit">Volts</span>
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

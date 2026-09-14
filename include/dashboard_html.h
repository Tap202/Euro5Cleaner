#pragma once
#include <Arduino.h>

const char INDEX_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
  <meta name="theme-color" content="#080b12">
  <meta name="apple-mobile-web-app-capable" content="yes">
  <meta name="apple-mobile-web-app-status-bar-style" content="black-translucent">
  <meta name="apple-mobile-web-app-title" content="MT-09 SP">
  <title>Yamaha MT-09 SP | Live CAN Cockpit</title>
  <link rel="preconnect" href="https://fonts.googleapis.com">
  <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
  <link href="https://fonts.googleapis.com/css2?family=Outfit:wght@300;400;500;600;700;800;900&family=JetBrains+Mono:wght@400;600;700;800&display=swap" rel="stylesheet">
  <style>
    :root {
      --bg-base: #07090e;
      --bg-card: rgba(15, 20, 32, 0.7);
      --bg-card-hover: rgba(22, 30, 48, 0.8);
      --border-subtle: rgba(255, 255, 255, 0.08);
      --border-glow: rgba(0, 229, 255, 0.3);
      
      --cyan: #00e5ff;
      --cyan-glow: rgba(0, 229, 255, 0.25);
      --blue: #3b82f6;
      --green: #10b981;
      --green-glow: rgba(16, 185, 129, 0.25);
      --amber: #f59e0b;
      --amber-glow: rgba(245, 158, 11, 0.3);
      --red: #ef4444;
      --red-glow: rgba(239, 68, 68, 0.35);

      --text-main: #f8fafc;
      --text-muted: #94a3b8;
      --text-dim: #64748b;
    }

    body.sunlight-mode {
      --bg-base: #000000;
      --bg-card: #121212;
      --bg-card-hover: #1c1c1c;
      --border-subtle: #333333;
      --border-glow: #ffff00;
      --cyan: #ffff00;
      --cyan-glow: rgba(255, 255, 0, 0.3);
      --text-main: #ffffff;
      --text-muted: #e2e8f0;
      --text-dim: #cbd5e1;
      background: #000000 !important;
      background-image: none !important;
    }

    * {
      box-sizing: border-box;
      margin: 0;
      padding: 0;
      -webkit-tap-highlight-color: transparent;
    }

    body {
      font-family: 'Outfit', -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
      background: var(--bg-base);
      background-image: 
        radial-gradient(circle at 50% -10%, rgba(0, 229, 255, 0.12) 0%, transparent 60%),
        radial-gradient(circle at 50% 110%, rgba(59, 130, 246, 0.08) 0%, transparent 50%);
      color: var(--text-main);
      min-height: 100vh;
      padding: 12px 12px 28px 12px;
      display: flex;
      flex-direction: column;
      align-items: center;
      overflow-x: hidden;
      transition: background 0.25s ease, color 0.25s ease;
    }

    body.shift-flash {
      animation: shift-strobe 0.1s infinite alternate;
    }

    @keyframes shift-strobe {
      0% { box-shadow: inset 0 0 45px rgba(255, 0, 60, 0.85); }
      100% { box-shadow: inset 0 0 10px rgba(255, 255, 255, 0.9); }
    }

    .app-shell {
      width: 100%;
      max-width: 500px;
      display: flex;
      flex-direction: column;
      gap: 12px;
    }

    /* Minimal Top Navigation */
    .top-bar {
      display: flex;
      justify-content: space-between;
      align-items: center;
      padding: 4px 2px;
    }

    .brand-title {
      display: flex;
      align-items: baseline;
      gap: 8px;
    }

    .brand-title h1 {
      font-size: 1.15rem;
      font-weight: 800;
      letter-spacing: 0.8px;
      background: linear-gradient(135deg, #ffffff 30%, var(--cyan) 100%);
      -webkit-background-clip: text;
      -webkit-text-fill-color: transparent;
      text-transform: uppercase;
    }

    .brand-badge {
      font-size: 0.65rem;
      font-weight: 700;
      padding: 2px 7px;
      border-radius: 6px;
      background: rgba(0, 229, 255, 0.12);
      color: var(--cyan);
      border: 1px solid rgba(0, 229, 255, 0.25);
      letter-spacing: 0.5px;
    }

    .top-actions {
      display: flex;
      align-items: center;
      gap: 8px;
    }

    .btn-icon {
      background: rgba(255, 255, 255, 0.05);
      border: 1px solid var(--border-subtle);
      color: var(--text-main);
      padding: 6px 10px;
      border-radius: 12px;
      font-size: 0.85rem;
      cursor: pointer;
      display: flex;
      align-items: center;
      justify-content: center;
    }

    .link-status {
      display: flex;
      align-items: center;
      gap: 6px;
      font-size: 0.72rem;
      font-weight: 600;
      color: var(--text-muted);
      background: rgba(255, 255, 255, 0.04);
      border: 1px solid var(--border-subtle);
      padding: 5px 10px;
      border-radius: 20px;
    }

    .status-dot {
      width: 7px;
      height: 7px;
      border-radius: 50%;
      background: var(--green);
      box-shadow: 0 0 8px var(--green);
      animation: pulse-dot 1.8s infinite;
    }

    .status-dot.sim { background: var(--cyan); box-shadow: 0 0 8px var(--cyan); animation: pulse-dot 1s infinite; }
    .status-dot.offline { background: var(--red); box-shadow: 0 0 8px var(--red); animation: none; }

    @keyframes pulse-dot {
      0%, 100% { opacity: 1; transform: scale(1); }
      50% { opacity: 0.4; transform: scale(0.85); }
    }

    /* Autonomous Cleaner Status Strip */
    .autoclean-tag {
      background: rgba(16, 185, 129, 0.08);
      border: 1px solid rgba(16, 185, 129, 0.2);
      border-radius: 12px;
      padding: 8px 14px;
      display: flex;
      align-items: center;
      justify-content: space-between;
      font-size: 0.74rem;
      color: var(--green);
      font-weight: 600;
    }

    .autoclean-badge {
      font-size: 0.65rem;
      background: rgba(16, 185, 129, 0.18);
      padding: 2px 7px;
      border-radius: 6px;
      letter-spacing: 0.5px;
      text-transform: uppercase;
    }

    /* Engine, Health & Battery Quick Strip */
    .status-strip {
      display: grid;
      grid-template-columns: 1fr 1fr 1fr;
      gap: 8px;
    }

    .pill-card {
      background: var(--bg-card);
      border: 1px solid var(--border-subtle);
      backdrop-filter: blur(16px);
      -webkit-backdrop-filter: blur(16px);
      border-radius: 14px;
      padding: 8px 10px;
      display: flex;
      flex-direction: column;
      align-items: center;
      justify-content: center;
      gap: 3px;
    }

    .pill-label {
      font-size: 0.62rem;
      text-transform: uppercase;
      letter-spacing: 0.5px;
      color: var(--text-dim);
      font-weight: 700;
    }

    .pill-value {
      font-size: 0.82rem;
      font-weight: 700;
      font-family: 'JetBrains Mono', monospace;
      display: flex;
      align-items: center;
      gap: 4px;
    }

    .pill-eng-off { color: var(--text-muted); }
    .pill-eng-run { color: var(--amber); text-shadow: 0 0 10px var(--amber-glow); }
    .pill-clean { color: var(--green); }
    .pill-fault { color: var(--red); text-shadow: 0 0 10px var(--red-glow); }
    .pill-volt-ok { color: var(--cyan); }
    .pill-volt-warn { color: var(--red); animation: pulse-dot 1s infinite; }

    /* Primary Cockpit Card (Hero RPM, Gear, Speed) */
    .hero-cockpit {
      background: linear-gradient(180deg, rgba(20, 27, 44, 0.75) 0%, rgba(11, 15, 26, 0.85) 100%);
      border: 1px solid var(--border-subtle);
      border-radius: 24px;
      padding: 20px 18px;
      box-shadow: 0 10px 30px -10px rgba(0, 0, 0, 0.7);
      backdrop-filter: blur(20px);
      -webkit-backdrop-filter: blur(20px);
      display: flex;
      flex-direction: column;
      gap: 14px;
      position: relative;
      overflow: hidden;
    }

    .hero-cockpit::after {
      content: "";
      position: absolute;
      top: 0;
      left: 15%;
      right: 15%;
      height: 1px;
      background: linear-gradient(90deg, transparent, rgba(0, 229, 255, 0.4), transparent);
    }

    .hero-top-row {
      display: flex;
      justify-content: space-between;
      align-items: center;
    }

    .gear-badge-box {
      display: flex;
      align-items: center;
      gap: 8px;
    }

    .gear-badge {
      width: 44px;
      height: 44px;
      border-radius: 12px;
      background: rgba(0, 229, 255, 0.12);
      border: 2px solid var(--cyan);
      display: flex;
      align-items: center;
      justify-content: center;
      font-family: 'JetBrains Mono', monospace;
      font-size: 1.8rem;
      font-weight: 900;
      color: #fff;
      box-shadow: 0 0 16px var(--cyan-glow);
    }

    .gear-badge.neutral {
      border-color: var(--green);
      color: var(--green);
      box-shadow: 0 0 16px var(--green-glow);
      background: rgba(16, 185, 129, 0.15);
    }

    .gear-label {
      font-size: 0.65rem;
      text-transform: uppercase;
      color: var(--text-dim);
      font-weight: 700;
      letter-spacing: 0.5px;
    }

    .speed-box {
      display: flex;
      flex-direction: column;
      align-items: flex-end;
    }

    .speed-val-group {
      display: flex;
      align-items: baseline;
      gap: 4px;
    }

    .speed-number {
      font-family: 'JetBrains Mono', monospace;
      font-size: 2.3rem;
      font-weight: 800;
      line-height: 1;
      color: #ffffff;
    }

    .speed-unit {
      font-size: 0.75rem;
      font-weight: 700;
      color: var(--text-muted);
      cursor: pointer;
    }

    .rpm-display {
      display: flex;
      align-items: baseline;
      justify-content: center;
      gap: 6px;
      margin: 2px 0;
    }

    .rpm-number {
      font-family: 'JetBrains Mono', monospace;
      font-size: 4.5rem;
      font-weight: 900;
      line-height: 1;
      letter-spacing: -2px;
      color: #ffffff;
      text-shadow: 0 0 25px rgba(255, 255, 255, 0.15);
      transition: color 0.12s ease;
    }

    .rpm-unit {
      font-size: 1.1rem;
      font-weight: 700;
      color: var(--cyan);
      letter-spacing: 0.5px;
    }

    .tacho-track {
      width: 100%;
      height: 14px;
      background: rgba(255, 255, 255, 0.05);
      border-radius: 8px;
      overflow: hidden;
      position: relative;
      border: 1px solid rgba(255, 255, 255, 0.05);
    }

    .tacho-fill {
      height: 100%;
      width: 0%;
      border-radius: 8px;
      background: linear-gradient(90deg, var(--cyan) 0%, var(--blue) 55%, var(--amber) 85%, var(--red) 100%);
      box-shadow: 0 0 16px var(--cyan-glow);
      transition: width 0.12s cubic-bezier(0.1, 0.7, 0.1, 1);
    }

    .tacho-scale {
      display: flex;
      justify-content: space-between;
      font-family: 'JetBrains Mono', monospace;
      font-size: 0.65rem;
      color: var(--text-dim);
      font-weight: 600;
      padding: 0 2px;
    }

    /* Real-Time Lean Angle Card */
    .lean-card {
      background: var(--bg-card);
      border: 1px solid var(--border-subtle);
      border-radius: 20px;
      padding: 16px 18px;
      display: flex;
      flex-direction: column;
      gap: 10px;
      backdrop-filter: blur(16px);
      -webkit-backdrop-filter: blur(16px);
      position: relative;
      overflow: hidden;
    }

    .lean-header {
      display: flex;
      justify-content: space-between;
      align-items: center;
    }

    .lean-title {
      font-size: 0.74rem;
      font-weight: 700;
      text-transform: uppercase;
      letter-spacing: 0.8px;
      color: var(--text-muted);
      display: flex;
      align-items: center;
      gap: 6px;
    }

    .lean-reset-btn {
      background: transparent;
      border: none;
      color: var(--text-dim);
      font-size: 0.65rem;
      cursor: pointer;
      font-weight: 600;
    }
    .lean-reset-btn:hover { color: var(--cyan); }

    .lean-visual-wrap {
      display: flex;
      align-items: center;
      justify-content: space-between;
      padding: 4px 0;
    }

    .lean-stat-side {
      display: flex;
      flex-direction: column;
      align-items: center;
      width: 70px;
    }

    .lean-stat-label {
      font-size: 0.62rem;
      color: var(--text-dim);
      font-weight: 700;
      text-transform: uppercase;
    }

    .lean-stat-val {
      font-family: 'JetBrains Mono', monospace;
      font-size: 1.1rem;
      font-weight: 800;
      color: var(--text-main);
    }

    .lean-center-gauge {
      display: flex;
      flex-direction: column;
      align-items: center;
      position: relative;
    }

    .lean-bike-icon {
      font-size: 2.2rem;
      transition: transform 0.08s ease-out;
      display: inline-block;
      transform-origin: center bottom;
    }

    .lean-current-degrees {
      font-family: 'JetBrains Mono', monospace;
      font-size: 1.7rem;
      font-weight: 900;
      color: var(--cyan);
      margin-top: 4px;
      letter-spacing: -0.5px;
    }

    /* Lean Angle Scale Track */
    .lean-track {
      width: 100%;
      height: 8px;
      background: rgba(255, 255, 255, 0.06);
      border-radius: 6px;
      position: relative;
      overflow: hidden;
    }

    .lean-pointer {
      position: absolute;
      top: 0;
      bottom: 0;
      width: 14px;
      background: var(--cyan);
      box-shadow: 0 0 10px var(--cyan);
      border-radius: 4px;
      left: 50%;
      transform: translateX(-50%);
      transition: left 0.08s ease-out;
    }

    /* 4-Tile Secondary Matrix */
    .tiles-grid {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 10px;
    }

    .tile-card {
      background: var(--bg-card);
      border: 1px solid var(--border-subtle);
      border-radius: 16px;
      padding: 12px 14px;
      display: flex;
      flex-direction: column;
      justify-content: space-between;
      backdrop-filter: blur(16px);
      -webkit-backdrop-filter: blur(16px);
      min-height: 96px;
      transition: all 0.2s ease;
    }

    .tile-card:hover {
      border-color: rgba(255, 255, 255, 0.15);
      background: var(--bg-card-hover);
    }

    .tile-header {
      display: flex;
      justify-content: space-between;
      align-items: center;
    }

    .tile-label {
      font-size: 0.68rem;
      font-weight: 700;
      text-transform: uppercase;
      letter-spacing: 0.5px;
      color: var(--text-muted);
    }

    .tile-icon {
      font-size: 0.95rem;
      opacity: 0.85;
    }

    .tile-val-group {
      display: flex;
      align-items: baseline;
      gap: 4px;
      margin: 4px 0 6px 0;
    }

    .tile-value {
      font-family: 'JetBrains Mono', monospace;
      font-size: 1.85rem;
      font-weight: 800;
      line-height: 1;
      letter-spacing: -0.5px;
      color: #fff;
    }

    .tile-unit {
      font-size: 0.8rem;
      font-weight: 600;
      color: var(--text-dim);
    }

    .tile-sub {
      font-size: 0.65rem;
      color: var(--text-dim);
      font-family: 'JetBrains Mono', monospace;
    }

    .mini-bar {
      width: 100%;
      height: 4px;
      background: rgba(255, 255, 255, 0.06);
      border-radius: 4px;
      overflow: hidden;
    }

    .mini-bar-fill {
      height: 100%;
      width: 0%;
      border-radius: 4px;
      transition: width 0.2s ease;
    }

    .fill-tps { background: linear-gradient(90deg, #3b82f6, #8b5cf6); }

    /* Ride Data Logger Card */
    .logger-card {
      background: var(--bg-card);
      border: 1px solid var(--border-subtle);
      border-radius: 18px;
      padding: 14px 16px;
      display: flex;
      justify-content: space-between;
      align-items: center;
      backdrop-filter: blur(16px);
      -webkit-backdrop-filter: blur(16px);
    }

    .logger-info {
      display: flex;
      flex-direction: column;
      gap: 2px;
    }

    .logger-title {
      font-size: 0.75rem;
      font-weight: 700;
      text-transform: uppercase;
      letter-spacing: 0.6px;
      color: var(--text-main);
      display: flex;
      align-items: center;
      gap: 6px;
    }

    .logger-meta {
      font-size: 0.68rem;
      font-family: 'JetBrains Mono', monospace;
      color: var(--text-dim);
    }

    .logger-buttons {
      display: flex;
      gap: 8px;
    }

    .btn-log {
      padding: 8px 14px;
      border-radius: 10px;
      font-size: 0.75rem;
      font-weight: 700;
      border: none;
      cursor: pointer;
      display: flex;
      align-items: center;
      gap: 5px;
      transition: all 0.2s ease;
    }

    .btn-record {
      background: rgba(239, 68, 68, 0.18);
      border: 1px solid rgba(239, 68, 68, 0.4);
      color: #ff6b81;
    }
    .btn-record.recording {
      background: #ef4444;
      color: #fff;
      animation: pulse-dot 1s infinite;
    }

    .btn-export {
      background: rgba(0, 229, 255, 0.15);
      border: 1px solid rgba(0, 229, 255, 0.35);
      color: var(--cyan);
    }

    /* DTC Diagnostic Banner */
    .dtc-banner {
      background: var(--bg-card);
      border: 1px solid var(--border-subtle);
      border-radius: 16px;
      padding: 12px 14px;
      display: flex;
      flex-direction: column;
      gap: 8px;
      backdrop-filter: blur(16px);
      -webkit-backdrop-filter: blur(16px);
      transition: all 0.3s ease;
    }

    .dtc-banner.clean { border-color: rgba(16, 185, 129, 0.25); background: rgba(16, 185, 129, 0.05); }
    .dtc-banner.warning { border-color: rgba(239, 68, 68, 0.35); background: rgba(239, 68, 68, 0.08); }

    .dtc-header { display: flex; align-items: center; justify-content: space-between; }
    .dtc-headline { font-size: 0.8rem; font-weight: 700; display: flex; align-items: center; gap: 6px; }
    .dtc-clean-text { color: var(--green); }
    .dtc-fault-text { color: var(--red); }
    .dtc-time { font-size: 0.65rem; font-family: 'JetBrains Mono', monospace; color: var(--text-dim); }

    .dtc-list { display: flex; flex-direction: column; gap: 6px; margin-top: 4px; }
    .dtc-item {
      background: rgba(0, 0, 0, 0.3); border: 1px solid rgba(255, 255, 255, 0.08); border-radius: 10px; padding: 8px 12px; font-size: 0.78rem; line-height: 1.35;
    }
    .dtc-code { font-family: 'JetBrains Mono', monospace; font-weight: 700; color: #ffa4b6; margin-right: 6px; }
    .dtc-desc { color: #cbd5e1; font-weight: 400; }

    /* Action Controls */
    .actions-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; }
    .btn {
      border: none; outline: none; cursor: pointer; border-radius: 14px; padding: 13px 12px; font-family: inherit; font-weight: 700; font-size: 0.86rem;
      display: flex; flex-direction: column; align-items: center; justify-content: center; gap: 3px; transition: all 0.18s ease; touch-action: manipulation;
    }
    .btn:active:not(:disabled) { transform: scale(0.97); }
    .btn-scan {
      background: linear-gradient(135deg, rgba(0, 229, 255, 0.18) 0%, rgba(59, 130, 246, 0.1) 100%);
      border: 1px solid rgba(0, 229, 255, 0.35); color: var(--cyan);
    }
    .btn-scan:hover:not(:disabled) {
      background: linear-gradient(135deg, rgba(0, 229, 255, 0.28) 0%, rgba(59, 130, 246, 0.2) 100%); box-shadow: 0 4px 16px var(--cyan-glow);
    }
    .btn-clear {
      background: linear-gradient(135deg, rgba(239, 68, 68, 0.18) 0%, rgba(220, 38, 38, 0.08) 100%);
      border: 1px solid rgba(239, 68, 68, 0.35); color: #ff6b81;
    }
    .btn-clear:hover:not(:disabled) {
      background: linear-gradient(135deg, rgba(239, 68, 68, 0.28) 0%, rgba(220, 38, 38, 0.18) 100%); box-shadow: 0 4px 16px var(--red-glow);
    }
    .btn:disabled { opacity: 0.38; cursor: not-allowed; box-shadow: none !important; transform: none !important; }
    .btn-sub { font-size: 0.63rem; font-weight: 500; opacity: 0.75; letter-spacing: 0.2px; }

    /* Collapsible Diagnostics Console */
    .drawer-card {
      background: var(--bg-card); border: 1px solid var(--border-subtle); border-radius: 16px; overflow: hidden; backdrop-filter: blur(16px);
      -webkit-backdrop-filter: blur(16px); transition: all 0.3s ease;
    }
    .drawer-header {
      padding: 12px 16px; display: flex; justify-content: space-between; align-items: center; cursor: pointer; user-select: none;
      font-size: 0.76rem; font-weight: 600; color: var(--text-dim);
    }
    .drawer-header:hover { color: var(--text-muted); }
    .drawer-chevron { transition: transform 0.25s ease; font-size: 0.75rem; }
    .drawer-card.open .drawer-chevron { transform: rotate(180deg); }
    .drawer-content {
      max-height: 0; overflow: hidden; transition: max-height 0.3s cubic-bezier(0, 1, 0, 1); padding: 0 16px; display: flex; flex-direction: column; gap: 10px;
    }
    .drawer-card.open .drawer-content { max-height: 520px; padding: 0 16px 16px 16px; transition: max-height 0.3s ease-in-out; }
    .stats-grid { display: grid; grid-template-columns: 1fr 1fr 1fr; gap: 8px; padding-top: 6px; }
    .stat-box { background: rgba(0, 0, 0, 0.3); border: 1px solid var(--border-subtle); border-radius: 10px; padding: 8px; text-align: center; }
    .stat-label { font-size: 0.6rem; color: var(--text-dim); text-transform: uppercase; font-weight: 600; }
    .stat-val { font-family: 'JetBrains Mono', monospace; font-size: 0.85rem; font-weight: 700; color: var(--text-main); margin-top: 2px; }
    
    .nvs-bar {
      background: rgba(0, 0, 0, 0.25); border: 1px solid var(--border-subtle); border-radius: 10px; padding: 8px 12px;
      display: flex; justify-content: space-between; font-size: 0.72rem; color: var(--text-muted);
    }

    .ota-link-row {
      display: flex; justify-content: flex-end; align-items: center; font-size: 0.75rem;
    }
    .ota-link {
      color: var(--cyan); text-decoration: none; font-weight: 700; display: flex; align-items: center; gap: 4px;
    }

    .console-box {
      background: rgba(4, 6, 12, 0.85); border: 1px solid rgba(255, 255, 255, 0.05); border-radius: 10px; padding: 10px;
      font-family: 'JetBrains Mono', monospace; font-size: 0.68rem; height: 100px; overflow-y: auto; color: var(--text-muted);
      display: flex; flex-direction: column; gap: 3px;
    }
    .console-line { word-break: break-all; line-height: 1.35; }
    .console-line.hl { color: var(--cyan); font-weight: 600; }
    .console-line.ok { color: var(--green); }
    .console-line.err { color: var(--red); }
    .console-line.warn { color: var(--amber); }

    .toast {
      position: fixed; bottom: 20px; left: 50%; transform: translateX(-50%) translateY(100px); background: rgba(18, 24, 38, 0.95);
      border: 1px solid var(--border-subtle); box-shadow: 0 8px 30px rgba(0,0,0,0.6); backdrop-filter: blur(14px); padding: 12px 20px;
      border-radius: 14px; font-size: 0.82rem; font-weight: 600; color: #fff; display: flex; align-items: center; gap: 8px;
      opacity: 0; pointer-events: none; transition: all 0.3s cubic-bezier(0.16, 1, 0.3, 1); z-index: 1000; max-width: 90%; text-align: center;
    }
    .toast.show { transform: translateX(-50%) translateY(0); opacity: 1; }
  </style>
</head>
<body>

<div class="app-shell">
  <!-- Top Bar -->
  <header class="top-bar">
    <div class="brand-title">
      <h1>MT-09 SP</h1>
      <span class="brand-badge">EURO 5+</span>
    </div>
    <div class="top-actions">
      <!-- Sunlight Mode Toggle Button -->
      <button id="btnSunlight" class="btn-icon" onclick="toggleSunlightMode()" title="Toggle High-Contrast Sunlight Mode">
        <span id="sunIcon">☀️</span>
      </button>
      <div class="link-status">
        <span id="connDot" class="status-dot"></span>
        <span id="connText">Wi-Fi Active</span>
      </div>
    </div>
  </header>

  <!-- Autonomous Cleaner Status Banner -->
  <div class="autoclean-tag">
    <span>🛡️ Background Auto-Clear: <strong>ARMED</strong></span>
    <span id="txtAutoClearsBadge" class="autoclean-badge">0 Cleared</span>
  </div>

  <!-- Engine State, Health & Battery Quick Strip -->
  <div class="status-strip">
    <div class="pill-card">
      <span class="pill-label">Engine</span>
      <span id="pillEngine" class="pill-value pill-eng-off">
        <span>🛑</span>
        <span id="txtEngine">STOPPED</span>
      </span>
    </div>
    <div class="pill-card">
      <span class="pill-label">ECU Health</span>
      <span id="pillHealth" class="pill-value pill-clean">
        <span id="icoHealth">✓</span>
        <span id="txtHealth">CLEAN</span>
      </span>
    </div>
    <div class="pill-card">
      <span class="pill-label">Battery</span>
      <span id="pillBattery" class="pill-value pill-volt-ok">
        <span id="icoVolt">⚡</span>
        <span id="txtBattery">12.6V</span>
      </span>
    </div>
  </div>

  <!-- Primary Cockpit Gauge (RPM, Gear, Speed & Tachometer) -->
  <main class="hero-cockpit">
    <div class="hero-top-row">
      <!-- Live Gear Indicator -->
      <div class="gear-badge-box">
        <div id="gearBadge" class="gear-badge neutral">N</div>
        <div>
          <div class="gear-label">GEAR</div>
          <div id="shiftLightTag" style="font-size:0.62rem; color:var(--text-dim); font-weight:700;">SHIFT 9.8K</div>
        </div>
      </div>

      <!-- Vehicle Speed -->
      <div class="speed-box">
        <div class="speed-val-group">
          <span id="valSpeed" class="speed-number">0</span>
          <span id="unitSpeed" class="speed-unit" onclick="toggleSpeedUnit()">MPH</span>
        </div>
        <span style="font-size: 0.62rem; color: var(--text-dim); font-weight: 600;">VEHICLE SPEED</span>
      </div>
    </div>

    <div class="rpm-display">
      <span id="valRpm" class="rpm-number">0</span>
      <span class="rpm-unit">RPM</span>
    </div>

    <div class="tacho-track">
      <div id="barRpm" class="tacho-fill"></div>
    </div>

    <div class="tacho-scale">
      <span>0</span>
      <span>3K</span>
      <span>6K</span>
      <span>9K</span>
      <span>11.5K</span>
    </div>
  </main>

  <!-- MotoGP-Style Real-Time Lean Angle Gauge -->
  <section class="lean-card">
    <div class="lean-header">
      <span class="lean-title">🏍️ Real-Time Lean Angle (IMU)</span>
      <button class="lean-reset-btn" onclick="resetMaxLean()">Reset Max</button>
    </div>

    <div class="lean-visual-wrap">
      <div class="lean-stat-side">
        <span class="lean-stat-label">Max Left</span>
        <span id="valMaxLeft" class="lean-stat-val">0°</span>
      </div>

      <div class="lean-center-gauge">
        <span id="bikeIcon" class="lean-bike-icon">🏍️</span>
        <span id="valLeanAngle" class="lean-current-degrees">0°</span>
      </div>

      <div class="lean-stat-side">
        <span class="lean-stat-label">Max Right</span>
        <span id="valMaxRight" class="lean-stat-val">0°</span>
      </div>
    </div>

    <div class="lean-track">
      <div id="leanPointer" class="lean-pointer"></div>
    </div>
  </section>

  <!-- 4-Tile Secondary Matrix -->
  <section class="tiles-grid">
    <!-- Throttle Position (TPS) -->
    <div class="tile-card">
      <div class="tile-header">
        <span class="tile-label">Throttle</span>
        <span class="tile-icon">🎯</span>
      </div>
      <div class="tile-val-group">
        <span id="valTps" class="tile-value">0</span>
        <span class="tile-unit">%</span>
      </div>
      <div class="mini-bar">
        <div id="barTps" class="mini-bar-fill fill-tps"></div>
      </div>
    </div>

    <!-- Engine Coolant & IAT -->
    <div class="tile-card">
      <div class="tile-header">
        <span class="tile-label">Coolant / IAT</span>
        <span class="tile-icon">🌡️</span>
      </div>
      <div class="tile-val-group">
        <span id="valTemp" class="tile-value">--</span>
        <span class="tile-unit">°C</span>
      </div>
      <div class="tile-sub">Airbox (IAT): <span id="valIat">--°C</span></div>
    </div>

    <!-- 0–60 MPH Launch Timer -->
    <div class="tile-card">
      <div class="tile-header">
        <span class="tile-label">0-60 MPH Run</span>
        <span class="tile-icon">⏱️</span>
      </div>
      <div class="tile-val-group">
        <span id="valTimer" class="tile-value">0.00</span>
        <span class="tile-unit">s</span>
      </div>
      <div class="tile-sub">Status: <span id="timerStatus" style="color:var(--cyan);">ARMED (0 MPH)</span></div>
    </div>

    <!-- Stator Voltage & Charging -->
    <div class="tile-card">
      <div class="tile-header">
        <span class="tile-label">Stator Voltage</span>
        <span class="tile-icon">⚡</span>
      </div>
      <div class="tile-val-group">
        <span id="valVolts" class="tile-value">--</span>
        <span class="tile-unit">V</span>
      </div>
      <div class="tile-sub">System: <span id="statorHealth">STANDBY</span></div>
    </div>
  </section>

  <!-- Ride Data Logger & CSV Export Card -->
  <section class="logger-card">
    <div class="logger-info">
      <span class="logger-title">📊 Ride Telemetry Logger</span>
      <span id="logMeta" class="logger-meta">0 samples | 00:00</span>
    </div>
    <div class="logger-buttons">
      <button id="btnRecord" class="btn-log btn-record" onclick="toggleRideRecording()">
        <span id="recDot">●</span>
        <span id="recText">Record</span>
      </button>
      <button id="btnExport" class="btn-log btn-export" onclick="exportRideCsv()">
        <span>💾 Export CSV</span>
      </button>
    </div>
  </section>

  <!-- DTC Diagnostic Banner -->
  <section id="dtcBanner" class="dtc-banner clean">
    <div class="dtc-header">
      <div class="dtc-headline">
        <span id="dtcIcon">✓</span>
        <span id="dtcHeadlineText" class="dtc-clean-text">System Clean</span>
      </div>
      <span id="dtcTime" class="dtc-time">Ready</span>
    </div>
    <div id="dtcListContainer" class="dtc-list" style="display: none;"></div>
  </section>

  <!-- Action Controls -->
  <div class="actions-grid">
    <button id="btnScan" class="btn btn-scan" onclick="triggerDtcScan()">
      <span>🔍 Scan Codes</span>
      <span class="btn-sub">Query Active & Pending</span>
    </button>
    <button id="btnClear" class="btn btn-clear" onclick="triggerDtcClear()">
      <span>🧹 Clear Codes</span>
      <span class="btn-sub">Engine-OFF Protected</span>
    </button>
  </div>

  <!-- Collapsible Diagnostics Console -->
  <div id="drawerCard" class="drawer-card">
    <div class="drawer-header" onclick="toggleDrawer()">
      <span>⚙️ Diagnostics, NVS History & OTA</span>
      <span id="drawerChevron" class="drawer-chevron">▼</span>
    </div>
    <div class="drawer-content">
      <div class="stats-grid">
        <div class="stat-box">
          <div class="stat-label">Bus FPS</div>
          <div id="statFps" class="stat-val">0</div>
        </div>
        <div class="stat-box">
          <div class="stat-label">Total Frames</div>
          <div id="statFrames" class="stat-val">0</div>
        </div>
        <div class="stat-box">
          <div class="stat-label">TWAI Mode</div>
          <div id="statMode" class="stat-val">NORMAL</div>
        </div>
      </div>

      <!-- NVS Audit Trail -->
      <div class="nvs-bar">
        <span>💾 Total Auto-Clears: <strong id="nvsClears" style="color:var(--cyan);">0</strong></span>
        <span>Boot Count: <strong id="nvsBoots">1</strong></span>
        <span>Last: <strong id="nvsLastCode">None</strong></span>
      </div>

      <!-- Wireless OTA Update Link -->
      <div class="ota-link-row">
        <a href="/update" class="ota-link" target="_blank">📶 Wireless Firmware Update (/update) &rarr;</a>
      </div>

      <div id="consoleBox" class="console-box">
        <div class="console-line hl">🏍️ MT-09 SP Advanced Cockpit Suite Active.</div>
        <div class="console-line">Autonomous background DTC scanner & cleaner enabled.</div>
      </div>
    </div>
  </div>
</div>

<!-- Toast Popup -->
<div id="toast" class="toast">
  <span id="toastIcon">ℹ️</span>
  <span id="toastMsg">Notification message</span>
</div>

<script>
  let isEngineRunning = false;
  let isScanning = false;
  let isClearing = false;
  let isSimMode = false;
  let isSunlightMode = false;
  let speedUnitMph = true;

  const SHIFT_RPM = 9800;
  let timerState = 'ARMED';
  let launchStartTime = 0;
  let recorded0to60Time = null;

  // Lean angle tracking
  let currentLean = 0;
  let maxLeanLeft = 0;
  let maxLeanRight = 0;

  // Ride Logger tracking
  let isRecording = false;
  let recordedData = []; // Array of { time, rpm, speed, gear, tps, temp, iat, volts, lean }
  let logStartTime = 0;
  let logTimerInterval = null;

  // Simulator variables
  let simRpm = 1350;
  let simSpeedMph = 0;
  let simGear = 0;
  let simLean = 0;
  let simLeanDir = 1;
  let simDirection = 1;
  let simDtcCodes = [];

  const isLocalFile = window.location.protocol === 'file:' || window.location.hostname === 'localhost' || window.location.hostname === '127.0.0.1';

  function showToast(msg, icon = 'ℹ️', duration = 3000) {
    const toast = document.getElementById('toast');
    document.getElementById('toastIcon').innerText = icon;
    document.getElementById('toastMsg').innerText = msg;
    toast.className = 'toast show';
    setTimeout(() => { toast.className = 'toast'; }, duration);
  }

  function addLog(msg, type = '') {
    const box = document.getElementById('consoleBox');
    const div = document.createElement('div');
    div.className = 'console-line ' + type;
    const time = new Date().toLocaleTimeString();
    div.innerText = `[${time}] ${msg}`;
    box.appendChild(div);
    box.scrollTop = box.scrollHeight;
  }

  function toggleDrawer() {
    document.getElementById('drawerCard').classList.toggle('open');
  }

  function toggleSunlightMode() {
    isSunlightMode = !isSunlightMode;
    document.body.classList.toggle('sunlight-mode', isSunlightMode);
    document.getElementById('sunIcon').innerText = isSunlightMode ? '🌙' : '☀️';
    showToast(isSunlightMode ? 'High-Contrast Sunlight Mode Enabled' : 'Dark Mode Enabled', isSunlightMode ? '☀️' : '🌙', 2000);
  }

  function toggleSpeedUnit() {
    speedUnitMph = !speedUnitMph;
    document.getElementById('unitSpeed').innerText = speedUnitMph ? 'MPH' : 'KM/H';
  }

  function resetMaxLean() {
    maxLeanLeft = 0;
    maxLeanRight = 0;
    document.getElementById('valMaxLeft').innerText = '0°';
    document.getElementById('valMaxRight').innerText = '0°';
    if (!isSimMode && !isLocalFile) {
      fetch('/api/reset_lean', { method: 'POST' }).catch(() => {});
    }
    showToast('Max Lean Angle Reset!', '🔄', 2000);
  }

  // Ride Data Logging functions
  function toggleRideRecording() {
    isRecording = !isRecording;
    const btn = document.getElementById('btnRecord');
    const recDot = document.getElementById('recDot');
    const recText = document.getElementById('recText');

    if (isRecording) {
      btn.className = 'btn-log btn-record recording';
      recDot.innerText = '■';
      recText.innerText = 'Stop';
      logStartTime = Date.now();
      showToast('Ride Telemetry Recording Started!', '🔴');
      addLog('Ride Data Logger: Recording session started.', 'hl');

      logTimerInterval = setInterval(() => {
        const sec = Math.floor((Date.now() - logStartTime) / 1000);
        const m = String(Math.floor(sec / 60)).padStart(2, '0');
        const s = String(sec % 60).padStart(2, '0');
        document.getElementById('logMeta').innerText = `${recordedData.length.toLocaleString()} samples | ${m}:${s}`;
      }, 500);
    } else {
      btn.className = 'btn-log btn-record';
      recDot.innerText = '●';
      recText.innerText = 'Record';
      clearInterval(logTimerInterval);
      showToast(`Recording Stopped: ${recordedData.length} samples saved. Click "Export CSV" to download!`, '💾', 4000);
      addLog(`Ride Data Logger: Stopped. Total ${recordedData.length} samples captured.`, 'ok');
    }
  }

  function exportRideCsv() {
    if (recordedData.length === 0) {
      showToast('No ride data recorded yet! Click "Record" before riding.', '⚠️');
      return;
    }

    let csv = 'Timestamp_MS,RPM,Speed_MPH,Speed_KMH,Gear,Throttle_PCT,Coolant_C,IAT_C,Battery_Volts,Lean_Angle_Deg\n';
    recordedData.forEach(row => {
      csv += `${row.time},${row.rpm},${row.speed},${row.speedKmH},${row.gear},${row.tps},${row.temp},${row.iat},${row.volts},${row.lean}\n`;
    });

    const blob = new Blob([csv], { type: 'text/csv;charset=utf-8;' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    const dateStr = new Date().toISOString().slice(0, 10);
    a.href = url;
    a.download = `MT09_Ride_Data_${dateStr}.csv`;
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);
    URL.revokeObjectURL(url);

    showToast(`Exported ${recordedData.length} samples to CSV!`, '🎉');
    addLog(`CSV Export: Downloaded MT09_Ride_Data_${dateStr}.csv`, 'ok');
  }

  async function fetchTelemetry() {
    if (isLocalFile && !isSimMode) {
      activateSimMode();
      return;
    }

    try {
      const res = await fetch('/api/telemetry', { cache: 'no-store' });
      if (!res.ok) throw new Error('HTTP ' + res.status);
      const data = await res.json();
      updateDashboard(data);

      document.getElementById('connDot').className = 'status-dot';
      document.getElementById('connText').innerText = 'Wi-Fi Active';
    } catch (err) {
      if (!isSimMode) {
        activateSimMode();
      }
    }
  }

  function activateSimMode() {
    isSimMode = true;
    document.getElementById('connDot').className = 'status-dot sim';
    document.getElementById('connText').innerText = 'Browser Preview';
    addLog('Preview Mode active: Simulating motorcycle CAN telemetry, lean angle & ride logger.', 'hl');
  }

  function runSimulation() {
    if (!isSimMode) return;

    if (simDirection === 1) {
      simRpm += Math.floor(Math.random() * 260) + 160;
      simSpeedMph += 1.8;

      if (simRpm < 4500) simGear = 1;
      else if (simRpm < 7200) simGear = 2;
      else simGear = 3;

      // Simulate cornering lean
      simLean += simLeanDir * (Math.random() * 2.5 + 1.0);
      if (simLean > 45) simLeanDir = -1;
      if (simLean < -42) simLeanDir = 1;

      if (simRpm > 10600) {
        simDirection = -1;
      }
    } else {
      simRpm -= Math.floor(Math.random() * 300) + 200;
      simSpeedMph = Math.max(0, simSpeedMph - 2.5);

      if (simSpeedMph < 5) simGear = 0;
      else if (simSpeedMph < 25) simGear = 1;
      else simGear = 2;

      simLean *= 0.85; // Straighten up on deceleration

      if (simRpm < 1400) {
        simRpm = 1350 + Math.floor(Math.random() * 80);
        if (Math.random() > 0.6) simDirection = 1;
      }
    }

    const simTps = Math.max(0, Math.min(100, Math.round((simRpm - 1300) / 75)));
    const simTemp = 82;
    const simIat = 28;
    const simVolts = (simRpm > 400) ? 14.2 : 12.5;

    updateDashboard({
      rpm: simRpm,
      speed: Math.round(simSpeedMph),
      speedKmH: Math.round(simSpeedMph * 1.60934),
      gear: simGear,
      tps: simTps,
      temp: simTemp,
      iat: simIat,
      volts: simVolts,
      lean: parseFloat(simLean.toFixed(1)),
      maxLeanL: 42.0,
      maxLeanR: 45.0,
      eng: (simRpm > 400),
      fps: 148,
      frames: Math.floor(Date.now() / 10) % 50000,
      dtcCount: simDtcCodes.length,
      clears: 4,
      boots: 12,
      lastCode: 'P0036'
    });
  }

  function updateDashboard(data) {
    // 1. RPM & Tachometer
    const rpm = data.rpm || 0;
    document.getElementById('valRpm').innerText = rpm.toLocaleString();
    const rpmPct = Math.min(100, Math.max(0, (rpm / 11500) * 100));
    document.getElementById('barRpm').style.width = rpmPct + '%';

    if (rpm >= SHIFT_RPM) {
      document.body.classList.add('shift-flash');
    } else {
      document.body.classList.remove('shift-flash');
    }

    // 2. Gear Indicator
    const gear = data.gear !== undefined ? data.gear : 0;
    const gearBadge = document.getElementById('gearBadge');
    if (gear === 0) {
      gearBadge.className = 'gear-badge neutral';
      gearBadge.innerText = 'N';
    } else {
      gearBadge.className = 'gear-badge';
      gearBadge.innerText = gear;
    }

    // 3. Vehicle Speed
    const spd = speedUnitMph ? (data.speed || 0) : (data.speedKmH || 0);
    document.getElementById('valSpeed').innerText = spd;

    handleAccelerationTimer(data.speed || 0);

    // 4. Lean Angle Gauge
    if (data.lean !== undefined) {
      currentLean = data.lean;
      const leanEl = document.getElementById('valLeanAngle');
      const bikeEl = document.getElementById('bikeIcon');
      const pointerEl = document.getElementById('leanPointer');

      if (currentLean < 0) {
        leanEl.innerText = `◀ ${Math.abs(currentLean)}° L`;
      } else if (currentLean > 0) {
        leanEl.innerText = `${currentLean}° R ▶`;
      } else {
        leanEl.innerText = '0°';
      }

      // Rotate bike icon with angle
      bikeEl.style.transform = `rotate(${currentLean}deg)`;

      // Move slider pointer (-60° to +60° maps to 0% to 100%)
      const pct = Math.max(0, Math.min(100, 50 + (currentLean / 60) * 50));
      pointerEl.style.left = pct + '%';

      if (data.maxLeanL !== undefined) {
        maxLeanLeft = Math.max(maxLeanLeft, Math.abs(data.maxLeanL));
        document.getElementById('valMaxLeft').innerText = `${maxLeanLeft}°`;
      }
      if (data.maxLeanR !== undefined) {
        maxLeanRight = Math.max(maxLeanRight, data.maxLeanR);
        document.getElementById('valMaxRight').innerText = `${maxLeanRight}°`;
      }
    }

    // 5. TPS %
    const tps = data.tps || 0;
    document.getElementById('valTps').innerText = tps;
    document.getElementById('barTps').style.width = Math.min(100, tps) + '%';

    // 6. Coolant Temp & IAT
    const temp = data.temp;
    const tempEl = document.getElementById('valTemp');
    if (temp === -999 || temp === undefined || temp === null) {
      tempEl.innerText = '--';
    } else {
      tempEl.innerText = temp;
    }

    const iat = data.iat;
    if (iat !== -999 && iat !== undefined && iat !== null) {
      document.getElementById('valIat').innerText = iat + '°C';
    }

    // 7. Battery & Stator Voltage
    const volts = data.volts;
    const pillBat = document.getElementById('pillBattery');
    const txtBat = document.getElementById('txtBattery');
    const valVolts = document.getElementById('valVolts');
    const statorHealth = document.getElementById('statorHealth');

    if (volts && volts > 0) {
      txtBat.innerText = volts.toFixed(1) + 'V';
      valVolts.innerText = volts.toFixed(1);

      if (data.eng && volts < 12.4) {
        pillBat.className = 'pill-value pill-volt-warn';
        statorHealth.innerHTML = '<span style="color:var(--red);">LOW VOLTAGE (Stator Alert)</span>';
      } else if (volts >= 13.5) {
        pillBat.className = 'pill-value pill-volt-ok';
        statorHealth.innerHTML = '<span style="color:var(--green);">CHARGING OK</span>';
      } else {
        pillBat.className = 'pill-value pill-volt-ok';
        statorHealth.innerText = 'IGNITION ON';
      }
    }

    // 8. Engine Running State
    isEngineRunning = !!data.eng;
    const pillEng = document.getElementById('pillEngine');
    const btnClear = document.getElementById('btnClear');

    if (isEngineRunning) {
      pillEng.className = 'pill-value pill-eng-run';
      pillEng.innerHTML = '<span>🔥</span><span>RUNNING</span>';
      btnClear.disabled = true;
      btnClear.title = 'Cannot clear while engine is running';
    } else {
      pillEng.className = 'pill-value pill-eng-off';
      pillEng.innerHTML = '<span>🛑</span><span>STOPPED</span>';
      if (!isClearing && !isScanning) {
        btnClear.disabled = false;
      }
    }

    // 9. NVS History Stats
    if (data.clears !== undefined) {
      document.getElementById('txtAutoClearsBadge').innerText = `${data.clears} Cleared`;
      document.getElementById('nvsClears').innerText = data.clears;
    }
    if (data.boots !== undefined) {
      document.getElementById('nvsBoots').innerText = data.boots;
    }
    if (data.lastCode) {
      document.getElementById('nvsLastCode').innerText = data.lastCode;
    }

    // 10. Record Sample if Logger Active
    if (isRecording) {
      recordedData.push({
        time: Date.now() - logStartTime,
        rpm: data.rpm || 0,
        speed: data.speed || 0,
        speedKmH: data.speedKmH || 0,
        gear: data.gear || 0,
        tps: data.tps || 0,
        temp: data.temp || 0,
        iat: data.iat || 0,
        volts: (data.volts || 0).toFixed(1),
        lean: data.lean || 0
      });
    }

    // 11. Diagnostics
    if (data.dtcCount !== undefined) {
      updateHealthStatus(data.dtcCount);
    }
    if (data.fps !== undefined) {
      document.getElementById('statFps').innerText = data.fps;
    }
    if (data.frames !== undefined) {
      document.getElementById('statFrames').innerText = data.frames.toLocaleString();
    }
  }

  function handleAccelerationTimer(currentMph) {
    const timerVal = document.getElementById('valTimer');
    const timerStatus = document.getElementById('timerStatus');

    if (currentMph === 0) {
      if (timerState === 'FINISHED' || timerState === 'RUNNING') {
        timerState = 'ARMED';
        timerStatus.innerHTML = '<span style="color:var(--cyan);">ARMED (0 MPH)</span>';
      }
    } else if (currentMph > 0 && currentMph < 60) {
      if (timerState === 'ARMED') {
        timerState = 'RUNNING';
        launchStartTime = performance.now();
        timerStatus.innerHTML = '<span style="color:var(--amber);">LAUNCHING...</span>';
      }
      if (timerState === 'RUNNING') {
        const elapsed = (performance.now() - launchStartTime) / 1000;
        timerVal.innerText = elapsed.toFixed(2);
      }
    } else if (currentMph >= 60) {
      if (timerState === 'RUNNING') {
        const elapsed = (performance.now() - launchStartTime) / 1000;
        timerVal.innerText = elapsed.toFixed(2);
        timerState = 'FINISHED';
        recorded0to60Time = elapsed;
        timerStatus.innerHTML = `<span style="color:var(--green); font-weight:700;">PASSED! 0-60 in ${elapsed.toFixed(2)}s</span>`;
        showToast(`⚡ 0-60 MPH Completed in ${elapsed.toFixed(2)}s!`, '🏁', 4000);
      }
    }
  }

  function updateHealthStatus(count) {
    const pillHealth = document.getElementById('pillHealth');
    const icoHealth = document.getElementById('icoHealth');
    const txtHealth = document.getElementById('txtHealth');

    if (count > 0) {
      pillHealth.className = 'pill-value pill-fault';
      icoHealth.innerText = '⚠️';
      txtHealth.innerText = count + ' FAULT' + (count > 1 ? 'S' : '');
    } else {
      pillHealth.className = 'pill-value pill-clean';
      icoHealth.innerText = '✓';
      txtHealth.innerText = 'CLEAN';
    }
  }

  // Trigger DTC Scan
  async function triggerDtcScan() {
    if (isScanning) return;
    isScanning = true;
    const btn = document.getElementById('btnScan');
    btn.disabled = true;
    btn.innerHTML = '<span>⏳ Scanning...</span><span class="btn-sub">Querying ECU</span>';

    showToast('Querying ECU for trouble codes...', '🔍');
    addLog('Initiating OBD Mode 03/07 DTC Scan...', 'hl');

    if (isSimMode) {
      setTimeout(() => {
        simDtcCodes = [
          { code: "P0036", desc: "HO2S Heater Control Circuit (Bank 1 Sensor 2 - Post-Cat Exhaust / Akrapovič)" }
        ];
        renderDtcResults({ codes: simDtcCodes });
        isScanning = false;
        btn.disabled = false;
        btn.innerHTML = '<span>🔍 Scan Codes</span><span class="btn-sub">Query Active & Pending</span>';
      }, 600);
      return;
    }

    try {
      const res = await fetch('/api/scan');
      const data = await res.json();
      renderDtcResults(data);
    } catch (e) {
      showToast('Scan failed: ' + e.message, '❌');
      addLog('Scan failed: ' + e.message, 'err');
    } finally {
      isScanning = false;
      btn.disabled = false;
      btn.innerHTML = '<span>🔍 Scan Codes</span><span class="btn-sub">Query Active & Pending</span>';
    }
  }

  function renderDtcResults(data) {
    const banner = document.getElementById('dtcBanner');
    const icon = document.getElementById('dtcIcon');
    const headline = document.getElementById('dtcHeadlineText');
    const time = document.getElementById('dtcTime');
    const list = document.getElementById('dtcListContainer');

    time.innerText = new Date().toLocaleTimeString();
    list.innerHTML = '';

    const codes = data.codes || [];
    updateHealthStatus(codes.length);

    if (codes.length === 0) {
      banner.className = 'dtc-banner clean';
      icon.innerText = '✓';
      headline.className = 'dtc-clean-text';
      headline.innerText = 'System Clean (0 Faults)';
      list.style.display = 'none';
      showToast('ECU reports 0 fault codes stored!', '✅');
      addLog('Scan Complete: System clean, 0 codes.', 'ok');
    } else {
      banner.className = 'dtc-banner warning';
      icon.innerText = '⚠️';
      headline.className = 'dtc-fault-text';
      headline.innerText = `${codes.length} Fault Code${codes.length > 1 ? 's' : ''} Stored`;
      list.style.display = 'flex';

      codes.forEach(c => {
        const item = document.createElement('div');
        item.className = 'dtc-item';
        item.innerHTML = `<span class="dtc-code">${c.code}</span><span class="dtc-desc">${c.desc}</span>`;
        list.appendChild(item);
        addLog(`Fault Found: ${c.code} - ${c.desc}`, 'warn');
      });

      showToast(`Found ${codes.length} trouble code(s)!`, '⚠️');
    }
  }

  async function triggerDtcClear() {
    if (isClearing) return;

    if (isEngineRunning && !isSimMode) {
      showToast('SAFETY GUARD: Engine is RUNNING! Turn engine off (Key ON) to clear.', '🛑', 4000);
      addLog('Safety Guard: Clear attempt rejected because engine is RUNNING.', 'err');
      return;
    }

    if (!confirm('Safety Verification: Confirm the engine is OFF (Key switched ON). Proceed to clear trouble codes and reset Check Engine Light?')) {
      return;
    }

    isClearing = true;
    const btn = document.getElementById('btnClear');
    btn.disabled = true;
    btn.innerHTML = '<span>⏳ Clearing...</span><span class="btn-sub">Transmitting Mode 04</span>';

    showToast('Sending Mode 04 Clear command...', '🧹');
    addLog('Transmitting OBD Mode 04 clear command frame...', 'hl');

    if (isSimMode) {
      setTimeout(() => {
        simDtcCodes = [];
        showToast('Codes cleared & CEL reset!', '🎉', 4000);
        addLog('Clear Confirmed: Fault codes erased from ECU memory.', 'ok');
        renderDtcResults({ codes: [] });
        isClearing = false;
        btn.disabled = false;
        btn.innerHTML = '<span>🧹 Clear Codes</span><span class="btn-sub">Engine-OFF Protected</span>';
      }, 700);
      return;
    }

    try {
      const res = await fetch('/api/clear', { method: 'POST' });
      const data = await res.json();

      if (data.success) {
        showToast('Codes cleared & CEL reset!', '🎉', 4000);
        addLog('Clear Confirmed: Fault codes erased from ECU memory.', 'ok');
        setTimeout(triggerDtcScan, 600);
      } else {
        showToast('Clear rejected: ' + (data.message || 'Error'), '❌', 4000);
        addLog('Clear rejected: ' + data.message, 'err');
      }
    } catch (e) {
      showToast('Clear failed: ' + e.message, '❌');
      addLog('Clear network failure: ' + e.message, 'err');
    } finally {
      isClearing = false;
      btn.disabled = isEngineRunning;
      btn.innerHTML = '<span>🧹 Clear Codes</span><span class="btn-sub">Engine-OFF Protected</span>';
    }
  }

  setInterval(fetchTelemetry, 180);
  setInterval(runSimulation, 100);
  fetchTelemetry();
</script>
</body>
</html>
)rawliteral";

"""
ESP32 Market Tracker - LCD Simulator
Simulates Finnhub API calls and renders a fake LCD display using matplotlib.
Set FINNHUB_API_KEY in .env to use real data; leave blank for mock data.
"""

import os
import time
import random
import matplotlib.pyplot as plt
import matplotlib.patches as patches
from matplotlib.animation import FuncAnimation
from dotenv import load_dotenv

load_dotenv()

FINNHUB_API_KEY = os.getenv("FINNHUB_API_KEY", "")
USE_MOCK_DATA = not FINNHUB_API_KEY

STOCKS = ["NVDA", "AMD", "INTC", "VRT"]

_mock_prices = {"NVDA": 131.50, "AMD": 158.20, "INTC": 22.85, "VRT": 95.40}

def fetch_quote_mock(symbol: str) -> dict:
    base = _mock_prices[symbol]
    _mock_prices[symbol] = round(base + random.uniform(-0.5, 0.5), 2)
    price = _mock_prices[symbol]
    prev_close = round(price - random.uniform(-2.0, 2.0), 2)
    change = round(price - prev_close, 2)
    pct = round((change / prev_close) * 100, 2)
    return {"c": price, "d": change, "dp": pct, "pc": prev_close}


def fetch_quote_real(symbol: str) -> dict:
    import requests
    url = f"https://finnhub.io/api/v1/quote?symbol={symbol}&token={FINNHUB_API_KEY}"
    resp = requests.get(url, timeout=5)
    if resp.status_code == 429:
        raise ValueError(f"Rate limited (429)")
    resp.raise_for_status()
    data = resp.json()
    if data.get("c", 0) == 0:
        raise ValueError(f"No data returned for {symbol}")
    return data


def fetch_quote(symbol: str) -> dict | None:
    try:
        return fetch_quote_mock(symbol) if USE_MOCK_DATA else fetch_quote_real(symbol)
    except Exception as e:
        print(f"[{symbol}] fetch error: {e}")
        return None


# --- LCD renderer -----------------------------------------------------------
LCD_W, LCD_H = 480, 320
BG_COLOR = "#0a0a0a"
HEADER_COLOR = "#1a1a2e"
UP_COLOR = "#00e676"
DOWN_COLOR = "#ff5252"
NEUTRAL_COLOR = "#e0e0e0"
FONT = {"family": "monospace"}

fig, ax = plt.subplots(figsize=(LCD_W / 72, LCD_H / 72), dpi=144)
fig.patch.set_facecolor(BG_COLOR)
fig.canvas.manager.set_window_title("ESP32 Market Tracker — LCD Sim")

_last_quotes: dict = {}


def draw_frame(quotes: dict) -> None:
    ax.cla()
    ax.set_facecolor(BG_COLOR)
    ax.set_xlim(0, LCD_W)
    ax.set_ylim(0, LCD_H)
    ax.axis("off")

    # Header
    ax.add_patch(patches.FancyBboxPatch(
        (0, LCD_H - 22), LCD_W, 22,
        boxstyle="square,pad=0", linewidth=0, facecolor=HEADER_COLOR,
    ))
    ax.text(LCD_W / 2, LCD_H - 11, "MARKET TRACKER",
            color=NEUTRAL_COLOR, fontsize=7, ha="center", va="center",
            fontweight="bold", **FONT)
    ax.text(LCD_W - 4, LCD_H - 11, time.strftime("%H:%M:%S"),
            color="#888888", fontsize=5, ha="right", va="center", **FONT)

    # Stock rows
    row_h = (LCD_H - 22) / len(STOCKS)
    for i, symbol in enumerate(STOCKS):
        y_top = LCD_H - 22 - i * row_h
        y_mid = y_top - row_h / 2

        ax.add_patch(patches.FancyBboxPatch(
            (0, y_top - row_h), LCD_W, row_h,
            boxstyle="square,pad=0", linewidth=0,
            facecolor="#111111" if i % 2 == 0 else "#0d0d0d",
        ))

        q = quotes.get(symbol)
        if q is None:
            ax.text(8, y_mid, symbol, color="#555555", fontsize=8,
                    ha="left", va="center", fontweight="bold", **FONT)
            ax.text(LCD_W / 2, y_mid, "N/A", color="#555555",
                    fontsize=7, ha="center", va="center", **FONT)
            continue

        color = UP_COLOR if q["d"] >= 0 else DOWN_COLOR
        arrow = "▲" if q["d"] >= 0 else "▼"

        ax.text(8, y_mid, symbol, color=NEUTRAL_COLOR, fontsize=8,
                ha="left", va="center", fontweight="bold", **FONT)
        ax.text(LCD_W / 2, y_mid, f"${q['c']:.2f}", color=NEUTRAL_COLOR,
                fontsize=8, ha="center", va="center", **FONT)
        ax.text(LCD_W - 8, y_mid,
                f"{arrow}{abs(q['d']):.2f}  {q['dp']:+.2f}%",
                color=color, fontsize=7, ha="right", va="center", **FONT)

        ax.axhline(y=y_top - row_h, color="#222222", linewidth=0.5)

    fig.canvas.draw_idle()


def update(_frame):
    for sym in STOCKS:
        q = fetch_quote(sym)
        if q is not None:
            _last_quotes[sym] = q
    draw_frame(_last_quotes)


POLL_INTERVAL_MS = 2000 if USE_MOCK_DATA else 10000
ani = FuncAnimation(fig, update, interval=POLL_INTERVAL_MS, cache_frame_data=False)
update(0)
plt.tight_layout(pad=0)
plt.show()

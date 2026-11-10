#!/usr/bin/env python3
"""Refresh web/teams.json from ESPN (run occasionally; the build embeds the result).

ESPN's /teams endpoint doesn't send CORS headers, so the phone can't query it
directly. Instead we snapshot every league's teams and serve them from the ticker.
"""
import json, os, urllib.request

LEAGUES = [
    ("football", "nfl"), ("football", "college-football"),
    ("basketball", "nba"), ("basketball", "wnba"),
    ("basketball", "mens-college-basketball"), ("basketball", "womens-college-basketball"),
    ("baseball", "mlb"), ("hockey", "nhl"),
    ("soccer", "usa.1"), ("soccer", "eng.1"), ("soccer", "esp.1"), ("soccer", "ger.1"),
    ("soccer", "ita.1"), ("soccer", "fra.1"), ("soccer", "mex.1"), ("soccer", "uefa.champions"),
]
CDN = "https://a.espncdn.com/i/teamlogos/"

def get(url):
    req = urllib.request.Request(url, headers={"User-Agent": "sports-ticker-build"})
    with urllib.request.urlopen(req, timeout=30) as r:
        return json.load(r)

out = {}
for sport, league in LEAGUES:
    d = get(f"https://site.api.espn.com/apis/site/v2/sports/{sport}/{league}/teams?limit=1000")
    rows = []
    for item in d["sports"][0]["leagues"][0]["teams"]:
        t = item["team"]
        if t.get("isActive") is False:
            continue
        logos = t.get("logos") or [{}]
        dark = [l for l in logos if "dark" in (l.get("rel") or [])]
        logo = (dark or logos)[0].get("href", "")  # dark-background variant reads better in the UI
        logo = logo[len(CDN):] if logo.startswith(CDN) else logo
        rows.append([str(t["id"]), t.get("abbreviation", ""), t.get("displayName", t.get("name", "")),
                     (t.get("color") or "888888").lower(), logo])
    rows.sort(key=lambda r: r[2])
    out[f"{sport}/{league}"] = rows
    print(f"{sport}/{league}: {len(rows)} teams")

dst = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "web", "teams.json")
with open(dst, "w") as f:
    json.dump({"cdn": CDN, "leagues": out}, f, separators=(",", ":"))
print("wrote", dst, os.path.getsize(dst), "bytes")

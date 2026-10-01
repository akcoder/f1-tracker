#!/usr/bin/env python3
"""Shared Jolpica client for the build-time generators.

Jolpica is volunteer-run and donation-supported, publishes a 4 req/s burst and
500 req/hour sustained limit, blocks for abuse WITHOUT NOTICE, and has said the
limits will decrease (REQUIREMENTS.md 3.7.1, DATA-4). Their own guidance is to
cache and to use efficient queries. So:

  * every response is cached on disk - a rerun costs zero requests;
  * requests are spaced to stay well inside the burst limit;
  * an hourly budget is enforced locally and the generator stops rather than
    being stopped;
  * 4xx is never retried tightly (3.7.4) - it is fatal for that request shape.
"""
import hashlib, json, os, sys, time, urllib.error, urllib.request

BASE = "https://api.jolpi.ca/ergast/f1"
UA = {"User-Agent": "f1-tracker-build/1.0 (personal, non-commercial; github f1-tracker)"}
CACHE = os.path.join(os.path.dirname(os.path.abspath(__file__)), ".cache")

# The SUSTAINED limit is the binding one: 500 requests/hour is one request every
# 7.2 s on average, not the 4 req/s burst. Pacing to the burst earned an HTTP 429
# after 78 requests, well inside 500 - so there is a shorter rolling window in
# front of the hourly figure that the published limits do not describe.
MIN_GAP_S = 8.0           # ~450/hour, inside the published sustained limit
HOURLY_BUDGET = 480
MAX_BACKOFF_S = 600.0


class Client:
    def __init__(self, cache_dir=CACHE, verbose=True):
        self.cache = cache_dir
        self.verbose = verbose
        self.last = 0.0
        self.sent = 0
        self.hits = 0
        os.makedirs(self.cache, exist_ok=True)

    def _path(self, url):
        return os.path.join(self.cache, hashlib.sha1(url.encode()).hexdigest() + ".json")

    def get(self, path, **params):
        params.setdefault("format", "json")
        q = "&".join(f"{k}={v}" for k, v in params.items())
        url = f"{BASE}/{path.lstrip('/')}?{q}"
        p = self._path(url)
        if os.path.exists(p):
            self.hits += 1
            return json.load(open(p))

        if self.sent >= HOURLY_BUDGET:
            raise RuntimeError(
                f"local budget of {HOURLY_BUDGET} requests/hour reached. The cache in "
                f"{self.cache} is kept, so rerunning later resumes where this stopped.")

        gap = MIN_GAP_S - (time.time() - self.last)
        if gap > 0:
            time.sleep(gap)
        # 3.7.4 distinguishes these deliberately, and the first version of this
        # client did not: a 429 means "too fast", so the answer is to slow down
        # and resume. Any OTHER 4xx means the request shape is wrong, and
        # retrying it is what earns an IP block.
        backoff = 15.0
        while True:
            try:
                with urllib.request.urlopen(urllib.request.Request(url, headers=UA),
                                            timeout=45) as r:
                    d = json.load(r)
                break
            except urllib.error.HTTPError as e:
                if e.code != 429:
                    raise RuntimeError(
                        f"HTTP {e.code} for {url} - fatal for this request shape, "
                        f"not retried (3.7.4)") from None
                ra = e.headers.get("Retry-After")
                wait = float(ra) if (ra or "").isdigit() else backoff
                wait = min(wait, MAX_BACKOFF_S)
                if self.verbose:
                    print(f"    429 rate limited; backing off {wait:.0f}s "
                          f"(sent {self.sent})", file=sys.stderr)
                time.sleep(wait)
                backoff = min(backoff * 2, MAX_BACKOFF_S)
            except urllib.error.URLError as e:
                if self.verbose:
                    print(f"    network error {e.reason}; retry in {backoff:.0f}s",
                          file=sys.stderr)
                time.sleep(backoff)
                backoff = min(backoff * 2, MAX_BACKOFF_S)
        self.last = time.time()
        self.sent += 1
        json.dump(d, open(p, "w"))
        if self.verbose and self.sent % 25 == 0:
            print(f"    ... {self.sent} requests sent, {self.hits} served from cache",
                  file=sys.stderr)
        return d

    def total(self, path, **params):
        """The cheap career-count trick (decision 70): limit=1 and read the
        total, which returns a career figure in a few hundred bytes."""
        params["limit"] = 1
        return int(self.get(path, **params)["MRData"]["total"])

    def report(self):
        return f"{self.sent} requests sent, {self.hits} cache hits"

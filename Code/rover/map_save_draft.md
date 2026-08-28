# Draft: safe-path recording and return-to-base

## Goal

While an operator drives the rover, record the route as a network of verified-safe
segments. On request, plan a shortest return route that uses only those recorded
segments, then follow it conservatively.

## Assessment of the current notes and prototype

The overall idea is good: representing the driven route as a graph means the rover
can take a shorter previously travelled route back to base without cutting across
unknown ground. Saving points whenever the operator changes direction is also a
useful way to keep the map compact.

The main issue is the source of position. Two distance measurements alone normally
do not give an unambiguous 2D position; they define two circles, which can have two
possible intersections. Distance *differences* also do not reliably say which
direction the rover is travelling. The system needs either:

- wheel encoders plus an IMU for relative position (odometry), preferably corrected
  by external anchors when available; or
- three or more well-positioned anchors / another absolute positioning system.

The current `SafePathGraphEngine` is a useful starting point, but `_build_intersection_graph`
only connects segment endpoints. It does not yet calculate crossings, split segments
at them, or merge nearby repeated points, despite its docstring. The recorded
`safe_map` is also based on fixed sleep durations, so battery voltage, terrain and
wheel slip will quickly make it diverge from the real route.

## Proposed design

```text
Sensors -> pose estimator -> route recorder -> safe-path graph -> return planner -> motor controller
              |                   |                  |                  |
        (x, y, heading)       keypoints          Dijkstra/A*        closed-loop pose checks
```

### 1. Motion and pose layer

1. Use a motor-driver abstraction that supports forward, reverse and stop for each
   wheel. This accommodates the planned three-pin motor control and makes in-place
   turns possible.
2. Add wheel encoders. At a fixed rate (for example 20--50 Hz), convert encoder
   ticks to left/right wheel travel.
3. Fuse encoder movement with IMU heading. Store pose as `x`, `y`, `heading`, a
   timestamp, and a confidence/quality value.
4. If base-station data can provide an absolute fix, use it only to correct drift
   after validating the measurement. Keep recording if an anchor is temporarily
   unavailable.

### 2. Route recording

Record a pose sample continuously, but create a graph node only when one of these
events occurs:

- the operator starts/stops moving;
- heading changes by a threshold (e.g. 10 degrees);
- travelled distance since the previous node exceeds a threshold (e.g. 0.25 m);
- the rover returns within a merge radius of an existing node (e.g. 0.15 m).

Connect each new node to the previous node using the measured travelled distance.
Store the original intermediate samples on that edge; they are useful for precise
route following and later diagnostics.

### 3. Graph building and merging

Use a stable node ID and a small merge radius, not exact rounded coordinate equality.
When a new segment approaches an old node, split or join it only after a conservative
distance/heading check. Do not automatically treat a geometric line crossing as a
safe intersection: in real terrain, it may be a different level or an unsafe turn.
Only create a junction when the rover has actually driven through it.

Each edge should contain at least:

```python
Edge(from_id, to_id, length_m, samples, verified=True, traversal_count=1)
```

Its cost should initially be `length_m`, with optional penalties for sharp turns,
low localization confidence, or a previously failed traversal.

### 4. Return planning

1. Add the base as the first graph node.
2. On a return request, snap the current pose to the nearest *recently verified*
   edge/node only when it is within a safe tolerance; otherwise stop and request
   manual recovery.
3. Run Dijkstra on the verified graph from the snapped position to the base.
4. Convert the selected edge samples into short waypoints. Use a closed-loop
   waypoint controller: turn toward the next waypoint, drive slowly, and continuously
   compare expected pose with measured pose.
5. Stop immediately on excessive pose error, loss of localization, obstacle sensor
   trigger, or a motor/encoder inconsistency. Autonomous return should never assume
   that an unverified shortcut is safe.

## Suggested data model

```python
@dataclass
class Pose:
    timestamp: float
    x_m: float
    y_m: float
    heading_deg: float
    confidence: float

@dataclass
class PathNode:
    id: int
    pose: Pose
    kind: str  # base, turn, stop, merge, periodic

@dataclass
class PathEdge:
    from_id: int
    to_id: int
    length_m: float
    samples: list[Pose]
    verified: bool = True
```

Persist this as JSON after every new edge (write atomically through a temporary
file) so a restart does not discard the safe map.

## Incremental implementation plan

1. Separate hardware I/O from mapping logic. Replace `time.sleep`-derived movement
   values with a testable `PoseEstimator.update(...)` interface.
2. Create a simulator that feeds known encoder/heading samples into the estimator
   and verifies its recorded nodes and graph edges.
3. Implement `RouteRecorder`: node thresholds, merge-radius checks, JSON persistence,
   and a graph view of one manually driven route.
4. Implement Dijkstra over verified edges and unit-test paths containing loops and
   revisited locations.
5. Add a low-speed waypoint follower with a physical emergency stop and a maximum
   allowed localization error.
6. Calibrate wheel diameter, axle width, encoder ticks, turn accuracy and drift on a
   flat test course before any trench-adjacent testing.

## Immediate changes worth making in `map_save.py`

- Do not use `dist0 - dist1` as steering direction without a documented geometric
  model for the anchors and rover pose.
- Reset neither the graph nor the recorded route during an active mission; persist it.
- Make serial parsing frame-aware, because a single UART read can contain a partial
  frame or several frames.
- Keep GPIO/motor code out of the planner and ensure every error path commands stop.
- Add tests for command parsing, graph connectivity, shortest paths and malformed
  serial frames before connecting motors.

## Definition of a safe first milestone

On a flat, clear indoor course, manually drive a loop from a marked base. The software
should save the route, display its nodes and edges, plan a path back to base using
only recorded edges, and produce waypoints. Validate those waypoints in simulation
before allowing the rover to drive them at very low speed.

## More

It could be useful to make all this happen in a dedicated software based on gps(?) but it's not gonna be simple nor necessary.

in the end all that we're gonna actually show is the movement forward and backwards, the belt and the scan. All these things just need to be adapted to the version 2.0 of the design, but they are already working and ready.

I need to be careful not to overdo too much things, it could be overloading.

Tbf i started writing this just to get the daily 5 minutes so i could keep the streak going, so i don't really know what else to write, this has been 4 minutes until now. i really like how fast i've become in writing on the pc, my hands glide on the keyboard smoothly it's very relaxing to watch! lololol ok i'm out of things to write. please turn 5. now. or now. anytime now. come on. please. 6???? wtf are you kidding me. for some reason startime isn't counting the time... it still says itìs only 2 mins... dsjfhksdjfhksdhfskjdfsjdkfhskdf this is so annoying.
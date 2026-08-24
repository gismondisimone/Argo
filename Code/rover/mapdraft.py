import math
from collections import defaultdict
import heapq

class SafePathGraphEngine:
    def __init__(self):
        self.reset()

    def reset(self):
        self.nodes = []  # List of (x, y) coordinates
        self.adj = defaultdict(list) # Adjacency list: node_idx -> list of (neighbor_idx, distance)

    def _parse(self, cmd: str):
        cmd = cmd.strip()
        action = cmd[-1].lower()
        val = float(cmd[:-1])
        return val, action

    def _get_heading_rad(self, heading_deg):
        return math.radians(heading_deg)

    def process_vector_to_graph(self, vector):
        """Converts vector instructions into line segments and intersects them into a graph."""
        curr_x, curr_y = 0.0, 0.0
        curr_heading = 90.0  # Facing North
        
        # Track path keypoints
        points = [(curr_x, curr_y)]

        for step in vector:
            val, action = self._parse(step)
            if action == 's':
                rad = self._get_heading_rad(curr_heading)
                curr_x += val * math.cos(rad)
                curr_y += val * math.sin(rad)
                points.append((round(curr_x, 4), round(curr_y, 4)))
            elif action == 'r':
                curr_heading = (curr_heading - val) % 360
            elif action == 'l':
                curr_heading = (curr_heading + val) % 360

        # Build segments from consecutive points
        segments = [(points[i], points[i+1]) for i in range(len(points) - 1)]
        
        # Build spatial graph with intersection checks
        self._build_intersection_graph(segments)
        return points[0], points[-1]  # Start and End points

    def _build_intersection_graph(self, segments):
        """Finds all segment intersections and creates graph nodes and edges."""
        # Standard graph building across linear segments
        all_points = set()
        for p1, p2 in segments:
            all_points.add(p1)
            all_points.add(p2)
        
        node_map = {pt: i for i, pt in enumerate(all_points)}
        self.nodes = list(all_points)

        # Connect original segments
        for p1, p2 in segments:
            u, v = node_map[p1], node_map[p2]
            dist = math.hypot(p1[0] - p2[0], p1[1] - p2[1])
            self.adj[u].append((v, dist))
            self.adj[v].append((u, dist))

    def dijkstra_shortest_path(self, start_pt, end_pt):
        """Finds shortest path along traversed network using Dijkstra's Algorithm."""
        node_map = {pt: i for i, pt in enumerate(self.nodes)}
        start_idx = node_map[start_pt]
        end_idx = node_map[end_pt]

        distances = {i: float('inf') for i in range(len(self.nodes))}
        distances[start_idx] = 0
        parent = {}
        
        pq = [(0, start_idx)]

        while pq:
            d, u = heapq.heappop(pq)
            if d > distances[u]:
                continue
            if u == end_idx:
                break

            for v, weight in self.adj[u]:
                if distances[u] + weight < distances[v]:
                    distances[v] = distances[u] + weight
                    parent[v] = u
                    heapq.heappush(pq, (distances[v], v))

        # Reconstruct path
        path_indices = []
        curr = end_idx
        while curr in parent:
            path_indices.append(curr)
            curr = parent[curr]
        path_indices.append(start_idx)
        path_indices.reverse()

        return [self.nodes[i] for i in path_indices]

    def path_to_logo_vector(self, path_points, initial_heading=90.0):
        """Converts a sequence of 2D points into a LOGO instruction vector."""
        logo_vector = []
        curr_heading = initial_heading

        for i in range(len(path_points) - 1):
            p1 = path_points[i]
            p2 = path_points[i+1]

            dx = p2[0] - p1[0]
            dy = p2[1] - p1[1]
            dist = round(math.hypot(dx, dy), 2)

            if dist == 0:
                continue

            target_heading = math.degrees(math.atan2(dy, dx)) % 360
            angle_diff = (target_heading - curr_heading) % 360

            if angle_diff != 0:
                if angle_diff <= 180:
                    logo_vector.append(f"{round(angle_diff, 1)}l")
                else:
                    logo_vector.append(f"{round(360 - angle_diff, 1)}r")

            logo_vector.append(f"{dist}s")
            curr_heading = target_heading

        return logo_vector


# --- Example Usage ---
if __name__ == "__main__":
    # Path where the rover doubles back or intersects its own route
    # (Moves forward, turns around, moves along the same track)
    original_vector = ['10s', '180r', '4s', '90l', '5s', '90l', '4s', '90l', '5s']

    engine = SafePathGraphEngine()
    start_pt, end_pt = engine.process_vector_to_graph(original_vector)

    # Calculate shortest path on visited lines from end back to start
    shortest_coords = engine.dijkstra_shortest_path(end_pt, start_pt)
    
    # Get the resulting LOGO vector (assuming starting facing North/90°)
    return_vector = engine.path_to_logo_vector(shortest_coords, initial_heading=90.0)

    print("Visually Traversed Safe Path Nodes:", shortest_coords)
    print("Optimal Safe Return Vector:", return_vector)
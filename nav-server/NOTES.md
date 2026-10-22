# nav-server notes (measured on contabo, 4 cores / 7.8 GB, Vietnam extract 2026-10-21)

- First `refresh.sh` run: about 17 min end to end (download ~1 min, GraphHopper import + CH ~4 min,
  tiles ~10 min), exit 0.
- Import with `-Xmx5g`: JVM heap peaked near 2.5 GB total (log `totalMB:2484`), so 5g is ample.
- Graph on disk 569 MB; tiles 1.6 GB (158,658 tile files at zoom 15, 2.78 M ways).
- Serving: `-Xmx2g` loads the graph; container RSS about 1.3 GB. `/route?profile=moto_vn` works
  (Saigon centre to Tan Binh, 219 ms cold).
- Image CMD uses `-Xmx2g` and `graph.location=/data/graph`; mount `/root/nav/data` at `/data`.

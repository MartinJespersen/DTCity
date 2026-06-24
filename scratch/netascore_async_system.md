# Async NetaScore Loading
Prerequisites:
- Use a single cross-platform library for HTTP requests (e.g. curl or httplib)
- fetch GCPK or GeoJson?
## Flow:
1. Get API key 
2. Call endpoint /jobs/netascore with key and yield immediatly
  - Save job_id for use in next call
3. Call endpoint /jobs/{job_id}
  - check periodically for status
  - when job is done call next endpoint
4. Call /jobs/{job_id}/downloads
  - Save the key to fetch actual http results 
5. Call /jobs/{job_id}/download/{key}


## Things to do 
- Develop an async priority queue for the developing timer based queue

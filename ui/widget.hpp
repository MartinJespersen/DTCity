// UI MVP:
// 1. Find the point of intersection between ray and the first 3D geometric structure
// 2. Identify the object and its relation to OSM data structure
// 2a. Starting with roads
// 3. If data is available, display it on the screen
// 3a. Display road information: starting with the name of the road
// 4. Display the information in a pop-up window
//
// Widget capabilities for MVP:
// - Show text with a single font and single size
// - Make the the pop up be movable (in the long term by the user or depend on the mouse position)
// - The pop up should dynamically resize based on the content but it should also be possible for
// -    - the text to wrap at the max width
//
// Questions: What role does the viewport play in the UI? Should multiple be used?
// viewports be used?
//
// Hit Test Implementation:
// - Map the result of the depth buffer to a way id to identify find the closest object to the
// screen
// - Start mirroring the road and building pipeline (they have a way id for each road/building)
// - - Build a buffer with with 3D information (depth buffer calculation) and the way ID of the
// object
// - - Swapchain should include images (for each frame in flight) that will have a format like
// VK_FORMAT_R32_UINT (consider a larger value)
//

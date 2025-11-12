# Cesium Integration

## Important functions
### updateView
Called every frame to get the camera update information.
Camera info needed in updateView:
- Position
- View direction
- up vector
- view port width and height
- horizontal and vertical field-of-view angles

### viewstate constructor
Creation of the ViewState object use in updateView using different parameters

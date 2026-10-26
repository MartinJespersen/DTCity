
from pyproj import Transformer
# 3130210.5137672587	921001.9655893086	5461969.330648276
x = 3130210.5137672587
y = 921001.9655893086
z = 5461969.330648276
transformer = Transformer.from_crs("EPSG:4978", "EPSG:4326", always_xy=True)
lon = transformer.transform(x, y, z)
print("lon: ", lon)

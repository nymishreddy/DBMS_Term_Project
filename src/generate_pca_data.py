import numpy as np
from sklearn.decomposition import PCA
import struct

# TODO: Replace this with loading your actual extracted image embeddings
num_images = 10000
original_dim = 2048
print(f"Generating mock {original_dim}D embeddings for {num_images} images...")
raw_embeddings = np.random.rand(num_images, original_dim).astype(np.float32)

dimensions_to_test = [10, 20, 30, 50]

for target_dim in dimensions_to_test:
    print(f"\n--- Reducing to {target_dim}D ---")
    pca = PCA(n_components=target_dim)
    reduced = pca.fit_transform(raw_embeddings).astype(np.float32)
    
    filename = f"embeddings_{target_dim}D.bin"
    with open(filename, "wb") as f:
        # Write header: [num_records] [dim]
        f.write(struct.pack('ii', num_images, target_dim))
        # Write flat float payload
        f.write(reduced.tobytes())
    print(f"Exported {filename}")
#ifndef test_h
#define test_h  
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <omp.h>  // OpenMP header

#define MAX_SIZE 100
#define FLAG_MAX 9999
#define PREVIOUS_FILES 100

#endif

int* kmeans2(Uint8 DataArray[], int Location[], int N, unsigned D, unsigned K) {
    
    /* Starting Variables */
    unsigned int iteration = 0;
    int flagEnd = 0;
    clock_t start, end;

    /* Loop variables */
    register int i, j, d;

    int flag = 0;
    int flagPrev = 0;
 
    /*------Memory Allocation-----*/
    float *Centroids = (float*)calloc(K*D, sizeof(float));
    float *FlagCentroids = (float*)calloc(K*D, sizeof(float));
    int *Counter = (int*)calloc(K, sizeof(int));
    float *ClusterTotalSum = (float*)calloc(K*D, sizeof(float));
    float *Distance = (float*)calloc(N*K, sizeof(float));
    float *Min = (float*)calloc(N, sizeof(float));

    /*--------K-means++ Initialization (Parallelized)-----*/
    // First centroid - random selection
    Centroids[0*D + 0] = DataArray[0*D + 0];
    for (d = 1; d < D; ++d)
        Centroids[0*D + d] = DataArray[0*D + d];

    float *minDistSq = (float*)calloc(N, sizeof(float));
    if (!minDistSq) { printf("Memory error\n"); exit(1); }

    // Parallel distance computation for first centroid
    #pragma omp parallel for private(d) schedule(static)
    for (i = 0; i < N; ++i) {
        float dist = 0.0;
        for (d = 0; d < D; ++d) {
            float diff = DataArray[i*D + d] - Centroids[0*D + d];
            dist += diff * diff;
        }
        minDistSq[i] = dist;
    }

    // Select remaining K-1 centroids
    for (j = 1; j < K; ++j) {
        // Compute total (can be parallelized with reduction)
        float total = 0.0;
        #pragma omp parallel for reduction(+:total) schedule(static)
        for (i = 0; i < N; ++i)
            total += minDistSq[i];

        // Pick random value and find next centroid
        float r = ((float)rand() / RAND_MAX) * total;
        float cumSum = 0.0;
        int nextCentroid = N-1;
        for (i = 0; i < N; ++i) {
            cumSum += minDistSq[i];
            if (cumSum >= r) {
                nextCentroid = i;
                break;
            }
        }

        // Assign next centroid
        for (d = 0; d < D; ++d)
            Centroids[j*D + d] = DataArray[nextCentroid*D + d];

        // Update minDistSq in parallel
        #pragma omp parallel for private(d) schedule(static)
        for (i = 0; i < N; ++i) {
            float dist = 0.0;
            for (d = 0; d < D; ++d) {
                float diff = DataArray[i*D + d] - Centroids[j*D + d];
                dist += diff * diff;
            }
            if (dist < minDistSq[i])
                minDistSq[i] = dist;
        }
    }

    // Copy initial centroids
    #pragma omp parallel for collapse(2) schedule(static)
    for (j = 0; j < K; ++j)
        for (d = 0; d < D; ++d)
            FlagCentroids[j*D + d] = Centroids[j*D + d];

    free(minDistSq);

    start = clock();
    
    /*--Main K-means Algorithm Loop---*/
    do {
        // Reset counters and sums in parallel
        if(iteration > 0) {
            #pragma omp parallel for schedule(static)
            for(j = 0; j < K; ++j) {
                Counter[j] = 0;
                for(d = 0; d < D; ++d) {
                    ClusterTotalSum[j*D + d] = 0;
                }
            }
        }

        // MAIN COMPUTATION: Distance calculation and assignment (parallelized)
        #pragma omp parallel for private(j, d) schedule(static)
        for(i = 0; i < N; ++i) {
            Min[i] = FLAG_MAX;
            
            // Calculate distances to all centroids
            for(j = 0; j < K; ++j) {
                Distance[i*K + j] = 0;
                for(d = 0; d < D; ++d) {
                    float diff = DataArray[i*D + d] - Centroids[j*D + d];
                    Distance[i*K + j] += diff * diff;
                }
                Distance[i*K + j] = sqrt(Distance[i*K + j]);

                // Find minimum distance and assign cluster
                if(Distance[i*K + j] < Min[i]) {
                    Min[i] = Distance[i*K + j];
                    Location[i] = j;
                }
            }
        }

        // Accumulate cluster sums (this needs careful parallelization due to race conditions)
        // Using atomic operations or reduction clauses
        #pragma omp parallel
        {
            // Create thread-private arrays for accumulation
            int *localCounter = (int*)calloc(K, sizeof(int));
            float *localSum = (float*)calloc(K*D, sizeof(float));
            
            #pragma omp for nowait schedule(static)
            for(i = 0; i < N; ++i) {
                int cluster = Location[i];
                localCounter[cluster]++;
                for(d = 0; d < D; ++d) {
                    localSum[cluster*D + d] += DataArray[i*D + d];
                }
            }
            
            // Combine results from all threads
            #pragma omp critical
            {
                for(j = 0; j < K; ++j) {
                    Counter[j] += localCounter[j];
                    for(d = 0; d < D; ++d) {
                        ClusterTotalSum[j*D + d] += localSum[j*D + d];
                    }
                }
            }
            
            free(localCounter);
            free(localSum);
        }

        // Calculate new centroids in parallel
        #pragma omp parallel for collapse(2) schedule(static)
        for(j = 0; j < K; ++j) {
            for(d = 0; d < D; ++d) {
                if(Counter[j] > 0) {
                    Centroids[j*D + d] = ClusterTotalSum[j*D + d] / Counter[j];
                }
            }
        }

        // Check convergence (parallelized with early termination)
        flagEnd = -1; // Assume convergence
        #pragma omp parallel for collapse(2) schedule(static)
        for(j = 0; j < K; ++j) {
            for(d = 0; d < D; ++d) {
                if(FlagCentroids[j*D + d] != Centroids[j*D + d]) {
                    #pragma omp atomic write
                    flagEnd = 0;
                }
            }
        }

        // Copy new centroids to flag array
        #pragma omp parallel for collapse(2) schedule(static)
        for(j = 0; j < K; ++j) {
            for(d = 0; d < D; ++d) {
                FlagCentroids[j*D + d] = Centroids[j*D + d];
            }
        }

        ++iteration;

    } while(flagEnd != -1);

    end = clock();

    // Store final centroids
    #pragma omp parallel for schedule(static)
    for (int j = 0; j < K * D; ++j) {
        Location[N + j] = Centroids[j];
    }

    // Cleanup
    free(Centroids);
    free(FlagCentroids);
    free(Counter);
    free(ClusterTotalSum);
    free(Distance);
    free(Min);
    
    return Location;
}
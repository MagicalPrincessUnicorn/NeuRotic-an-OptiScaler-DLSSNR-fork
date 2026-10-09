namespace NeuRotic.Discovery;
internal static class InstallResolutionQueue
{
    public static void Run<T>(IEnumerable<T> records,Action<T> resolve,CancellationToken cancel)
    {
        // Enumerator access is serialized by Parallel.ForEach; no full-library task list is retained.
        Parallel.ForEach(records,new ParallelOptions{MaxDegreeOfParallelism=4,CancellationToken=cancel},record=>{
            // Parallel's partitioner may already hold a batch when cancellation arrives.
            cancel.ThrowIfCancellationRequested();resolve(record);
        });
    }
}

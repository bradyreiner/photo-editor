import QtQuick

QtObject {
    function failureStageLabel(stage) {
        const normalized = String(stage || "").replace(/_/g, " ").trim()
        if (normalized.length === 0)
            return "Thumbnail"
        return normalized.charAt(0).toUpperCase() + normalized.slice(1)
    }

    function thumbnailDisplayState(data) {
        const ready = Boolean(data && data.thumbnailReady === true)
        const stale = Boolean(data && data.thumbnailStale === true)
        const progressive = Boolean(data && data.thumbnailProgressive === true)
        const contained = Boolean(data && data.thumbnailFailureContained === true)
        const source = String(data && data.thumbnailSource !== undefined
                              ? data.thumbnailSource : "").trim()
        if (contained && stale)
            return "contained-failure-stale"
        if (contained)
            return "contained-failure"
        if (progressive && stale && source.length > 0)
            return "progressive-updating"
        if (ready && source.length > 0)
            return "ready"
        return "empty"
    }

    function thumbnailStateSignature(data) {
        const ready = Boolean(data && data.thumbnailReady === true)
        const stale = Boolean(data && data.thumbnailStale === true)
        const progressive = Boolean(data && data.thumbnailProgressive === true)
        const contained = Boolean(data && data.thumbnailFailureContained === true)
        const stage = String(data && data.thumbnailFailureStage !== undefined
                             ? data.thumbnailFailureStage : "").trim()
        const displayState = thumbnailDisplayState(data)
        return "ready:" + String(ready)
                + "|stale:" + String(stale)
                + "|progressive:" + String(progressive)
                + "|failure-contained:" + String(contained)
                + "|stage:" + stage
                + "|display:" + displayState
    }

    function thumbnailStatusLabel(data, fallbackText) {
        const contained = Boolean(data && data.thumbnailFailureContained === true)
        const stale = Boolean(data && data.thumbnailStale === true)
        const progressive = Boolean(data && data.thumbnailProgressive === true)
        const stage = String(data && data.thumbnailFailureStage !== undefined
                             ? data.thumbnailFailureStage : "").trim()
        if (contained && stale)
            return "Thumbnail stale / " + failureStageLabel(stage)
        if (contained)
            return "Thumbnail unavailable / " + failureStageLabel(stage)
        if (progressive && stale)
            return "Thumbnail updating"
        return String(fallbackText || "")
    }

    function thumbnailRetryActionSignature(data, rowKind) {
        const contained = Boolean(data && data.thumbnailFailureContained === true)
        const stage = String(data && data.thumbnailFailureStage !== undefined
                             ? data.thumbnailFailureStage : "").trim()
        const detail = String(data && data.thumbnailFailureDetail !== undefined
                              ? data.thumbnailFailureDetail : "").trim()
        return "thumbnail-retry|row:" + String(rowKind || "unknown")
                + "|available:" + String(contained)
                + "|stage:" + stage
                + "|detail:" + detail
    }

    function thumbnailRefreshQueued(data) {
        if (data && data.thumbnailQueuePending !== undefined)
            return data.thumbnailQueuePending === true
        const progressive = Boolean(data && data.thumbnailProgressive === true)
        const stale = Boolean(data && data.thumbnailStale === true)
        const reason = String(data && data.thumbnailProgressiveReason !== undefined
                              ? data.thumbnailProgressiveReason : "")
        return progressive && stale && reason === "thumbnail_refresh_deferred"
    }

    function thumbnailQueueSignature(data) {
        const queued = thumbnailRefreshQueued(data)
        const jobId = String(data && data.thumbnailQueueJobId !== undefined
                             ? data.thumbnailQueueJobId : "").trim()
        const priority = String(data && data.thumbnailQueuePriority !== undefined
                                ? data.thumbnailQueuePriority : "").trim()
        const reason = String(data && data.thumbnailQueueReason !== undefined
                              ? data.thumbnailQueueReason
                              : data && data.thumbnailProgressiveReason !== undefined
                                ? data.thumbnailProgressiveReason : "").trim()
        const generation = String(data && data.thumbnailQueueGeneration !== undefined
                                  ? data.thumbnailQueueGeneration : 0)
        const blockers = String(data && data.thumbnailQueueDeferBlockerSignature !== undefined
                                ? data.thumbnailQueueDeferBlockerSignature : "").trim()
        return "thumbnail-queue|deferred:" + String(queued)
                + "|job:" + jobId
                + "|priority:" + priority
                + "|generation:" + generation
                + "|reason:" + reason
                + "|blockers:" + blockers
    }

    function thumbnailStaleReasonLabel(data) {
        const stale = Boolean(data && data.thumbnailStale === true)
        const detected = Boolean(data && data.thumbnailStaleDetected === true)
        if (!stale || !detected)
            return ""
        const reason = String(data && data.thumbnailStaleDetectionReason !== undefined
                              ? data.thumbnailStaleDetectionReason
                              : data && data.thumbnailProgressiveReason !== undefined
                                ? data.thumbnailProgressiveReason : "").trim()
        const mismatch = Boolean(data && data.thumbnailStaleMismatch === true)
        if (reason.length === 0)
            return mismatch ? "Thumbnail stale: recipe changed" : "Thumbnail stale"
        return mismatch ? "Thumbnail stale: " + reason : "Thumbnail stale"
    }

    function thumbnailStaleSignature(data) {
        const stale = Boolean(data && data.thumbnailStale === true)
        const detected = Boolean(data && data.thumbnailStaleDetected === true)
        const mismatch = Boolean(data && data.thumbnailStaleMismatch === true)
        const reason = String(data && data.thumbnailStaleDetectionReason !== undefined
                              ? data.thumbnailStaleDetectionReason : "").trim()
        const previousHash = String(data && data.thumbnailStalePreviousRecipeHash !== undefined
                                    ? data.thumbnailStalePreviousRecipeHash : "").trim()
        const currentHash = String(data && data.thumbnailStaleCurrentRecipeHash !== undefined
                                   ? data.thumbnailStaleCurrentRecipeHash : "").trim()
        return "thumbnail-stale|stale:" + String(stale)
                + "|detected:" + String(detected)
                + "|mismatch:" + String(mismatch)
                + "|reason:" + reason
                + "|previous:" + previousHash
                + "|current:" + currentHash
    }
}

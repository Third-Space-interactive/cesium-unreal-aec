// Copyright 2020-2025 CesiumGS, Inc. and Contributors

#pragma once

#include "Containers/ArrayView.h"
#include "Math/Box.h"
#include "Math/Vector.h"

namespace CesiumPointCloudBounds {

/**
 * A per-axis percentile box over local-space points. ClipFraction in [0, 0.5)
 * clips that fraction off each end of each axis independently, so isolated
 * outlier coordinates cannot inflate the box.
 */
FBox ComputeTrimmedLocalBox(
    TArrayView<const FVector3f> LocalPoints,
    float ClipFraction);

/**
 * Union of boxes, excluding whole boxes whose center is more than
 * MadThreshold median-absolute-deviations from the median center on any axis.
 * Returns an invalid FBox if Boxes is empty.
 */
FBox UnionWithMadRejection(TArrayView<const FBox> Boxes, float MadThreshold);

} // namespace CesiumPointCloudBounds

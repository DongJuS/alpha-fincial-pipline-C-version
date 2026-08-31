//! Phase-zero boundary for the selected Rust datalake adapter.
//!
//! The Parquet encoder and S3-compatible object-store implementation arrive in
//! Phase 4. Keeping their features explicit here prevents optional Rust network
//! edges from entering the workspace before the benchmark decision gate.

#![forbid(unsafe_code)]

/// Feature names that constitute the selected datalake boundary.
pub const SELECTED_FEATURES: &[&str] = &["parquet-encoding", "s3-storage"];

/// Reports whether the complete selected datalake dependency boundary is built.
#[must_use]
pub const fn selected_boundary_enabled() -> bool {
    cfg!(feature = "datalake")
}

#[cfg(test)]
mod tests {
    use super::{selected_boundary_enabled, SELECTED_FEATURES};

    #[test]
    fn selected_boundary_has_only_the_approved_capabilities() {
        assert_eq!(SELECTED_FEATURES, ["parquet-encoding", "s3-storage"]);
    }

    #[cfg(feature = "datalake")]
    #[test]
    fn aggregate_feature_enables_the_selected_boundary() {
        assert!(selected_boundary_enabled());
    }

    #[cfg(not(feature = "datalake"))]
    #[test]
    fn default_build_does_not_enable_the_adapter_early() {
        assert!(!selected_boundary_enabled());
    }
}

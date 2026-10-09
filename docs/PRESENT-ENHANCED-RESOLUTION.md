# Present resolution policies

Native Temporal, Present Image-Only and guided Present retain separate configuration. The legacy PresentResolution and EnhancedResolution policies include Follow Native, full output and custom processing scales; PresentCustomScale and EnhancedCustomScale retain the custom choices. Current policies also include Manual (25–200 percent) and Automatic. Automatic uses output dimensions; processing dimensions remain bounded to 8192.

Follow Native uses fresh render-subrect metadata. Full output uses output dimensions. Reduced processing dimensions use the route's alignment and bounds rules. When qualified guide metadata is missing, Require Guides refuses. Auto Guides can use an image-only baseline with admitted dimensions or a full-output workload. A previously admitted workload is retained only within its valid context.

Guide origins, bounds, motion conventions and jitter travel with validated inputs. Motion and jitter scale once for the actual processing/output dimensions. Policy, route, dimensions and guide changes invalidate affected history. Resource replacement requires completion and recording-state proof.

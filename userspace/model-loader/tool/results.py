import math

from contract import digest, validate


def validate_decoder(document):
    validate(document)
    decoder = document["decoder"]
    if decoder["id"] != "segmentation.argmax" or decoder["version"] != 1:
        return None
    if decoder["result_schema"] != "segmentation/v1" or set(decoder["bindings"]) != {"scores"} or set(decoder["parameters"]) != {"class_axis"}:
        raise ValueError("segmentation decoder requires scores binding, class_axis and segmentation/v1")
    identifier = decoder["bindings"]["scores"]
    tensor = next(item for item in document["execution"]["outputs"] if item["id"] == identifier)
    meaning = next(item for item in document["semantics"]["outputs"] if item["id"] == identifier)
    axis = decoder["parameters"]["class_axis"]
    if type(axis) is not int or axis not in (1, 3) or len(tensor["shape"]) != 4 or tensor["shape"][0] != 1:
        raise ValueError("segmentation requires batch-one scores and an explicit class axis")
    expected_axes = ["batch", "class", "y", "x"] if axis == 1 else ["batch", "y", "x", "class"]
    if tensor["layout"] != (2 if axis == 1 else 1) or meaning.get("axes") != expected_axes:
        raise ValueError("segmentation tensor layout and semantic axes disagree")
    if meaning["meaning"] not in ("class_logits", "class_probabilities") or document["semantics"]["task"] != "semantic_segmentation":
        raise ValueError("segmentation output meaning is not declared")
    classes = tensor["shape"][axis]
    labels = document["semantics"]["labels"]
    if not 2 <= classes <= 65536 or [label["id"] for label in labels] != list(range(classes)):
        raise ValueError("multi-class segmentation requires a label for every class channel")
    return tensor, axis, labels


def decode_result(document, tensors):
    profile = validate_decoder(document)
    decoder = document["decoder"]
    report = {"schema_version": 1, "model_id": document["model_id"], "contract_sha256": digest(document),
              "decoder": decoder, "result_schema": decoder["result_schema"]}
    if decoder["id"] == "raw" and decoder["version"] == 1:
        report.update(status="raw", reason="output meaning is not interpreted")
        return report, None
    if profile is None:
        report.update(status="unsupported", reason="decoder ID/version is not supported")
        return report, None
    import numpy as np
    tensor, axis, labels = profile
    values = np.asarray(tensors[tensor["id"]])
    dtypes = (None, "u1", "i1", "<f4", "<i2", "<u2", "<f2", "<i4")
    if values.shape != tuple(tensor["shape"]) or values.dtype != np.dtype(dtypes[tensor["type"]]):
        raise ValueError("result tensor does not match its contract")
    if np.issubdtype(values.dtype, np.integer):
        values = (values.astype(np.float64) - tensor["zero"]) * tensor["scale"]
    if not np.isfinite(values).all():
        raise ValueError("non-finite segmentation scores")
    if next(item for item in document["semantics"]["outputs"] if item["id"] == tensor["id"])["meaning"] == "class_probabilities":
        if np.any(values < 0) or np.any(values > 1):
            raise ValueError("declared class probabilities are outside [0, 1]")
    mask = values[0].argmax(axis=axis - 1).astype(np.uint16 if len(labels) > 256 else np.uint8)
    counts = np.bincount(mask.ravel(), minlength=len(labels))
    report.update(status="decoded", result={"coordinate_space": "model_output_grid", "tensor_id": tensor["id"],
        "width": mask.shape[1], "height": mask.shape[0], "labels": labels,
        "classes": [{"class_id": label["id"], "pixels": int(count), "fraction": float(count / mask.size)}
                    for label, count in zip(labels, counts)]})
    return report, mask


def validate_policy(policy):
    if not isinstance(policy, dict) or set(policy) != {"schema_version", "id", "version", "parameters"}:
        raise ValueError("invalid policy document")
    if type(policy["schema_version"]) is not int or policy["schema_version"] != 1 or policy["id"] != "segmentation.area" or type(policy["version"]) is not int or policy["version"] != 1:
        raise ValueError("unsupported policy ID/version")
    parameters = policy["parameters"]
    if not isinstance(parameters, dict) or set(parameters) != {"class_id", "minimum_fraction"}:
        raise ValueError("area policy requires class_id and minimum_fraction")
    threshold = parameters["minimum_fraction"]
    if type(parameters["class_id"]) is not int or parameters["class_id"] < 0 or type(threshold) not in (int, float) or not math.isfinite(threshold) or not 0 <= threshold <= 1:
        raise ValueError("invalid class ID or area threshold")
    return policy


def evaluate_policy(report, policy):
    validate_policy(policy)
    decision = {"id": policy["id"], "version": policy["version"], "policy_sha256": digest(policy), "parameters": policy["parameters"]}
    if report["status"] != "decoded" or report["result_schema"] != "segmentation/v1":
        decision.update(status="unavailable", reason="policy requires a decoded segmentation result")
        return decision
    class_id = policy["parameters"]["class_id"]
    selected = next((item for item in report["result"]["classes"] if item["class_id"] == class_id), None)
    if selected is None:
        decision.update(status="unavailable", reason="requested class is not in the result contract")
        return decision
    decision.update(status="evaluated", matched=selected["fraction"] >= policy["parameters"]["minimum_fraction"],
                    fraction=selected["fraction"], coordinate_space=report["result"]["coordinate_space"])
    return decision
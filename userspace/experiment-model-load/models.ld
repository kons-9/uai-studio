SECTIONS
{
  OVERLAY 0x91a00000 :
  {
    .model_blob_person { KEEP(*(.model_blob_person)) }
    .model_blob_face { KEEP(*(.model_blob_face)) }
    .model_blob_seg { KEEP(*(.model_blob_seg)) }
  } > MODEL_BLOB
}
INSERT AFTER .experiment_blob;
ASSERT(SIZEOF(.model_blob_person) <= 0x200000, "person blob overflow")
ASSERT(SIZEOF(.model_blob_face) <= 0x200000, "face blob overflow")
ASSERT(SIZEOF(.model_blob_seg) <= 0x200000, "seg blob overflow")
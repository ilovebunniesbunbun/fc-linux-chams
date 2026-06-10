import os
import struct
import zlib

def make_vbib_block():
    # 1 Vertex Buffer, 1 Index Buffer
    vb_count = 1
    vertex_count = 3
    vertex_size = 32  # POSITION (12) + NORMAL (12) + TEXCOORD (8)
    attr_count = 3
    
    # POSITION attribute (28 bytes name + 24 bytes attributes = 52 bytes)
    attr1_name = b"POSITION" + b"\0" * 20
    attr1_index = 0
    attr1_format = 6  # R32G32B32_FLOAT
    attr1_offset = 0
    attr1_slot = 0
    attr1_slot_type = 0
    attr1_step_rate = 0
    attr1 = attr1_name + struct.pack("<IIIIII", attr1_index, attr1_format, attr1_offset, attr1_slot, attr1_slot_type, attr1_step_rate)
    
    # NORMAL attribute
    attr2_name = b"NORMAL" + b"\0" * 22
    attr2_index = 0
    attr2_format = 6  # R32G32B32_FLOAT
    attr2_offset = 12
    attr2_slot = 0
    attr2_slot_type = 0
    attr2_step_rate = 0
    attr2 = attr2_name + struct.pack("<IIIIII", attr2_index, attr2_format, attr2_offset, attr2_slot, attr2_slot_type, attr2_step_rate)
    
    # TEXCOORD attribute
    attr3_name = b"TEXCOORD" + b"\0" * 20
    attr3_index = 0
    attr3_format = 16  # R32G32_FLOAT
    attr3_offset = 24
    attr3_slot = 0
    attr3_slot_type = 0
    attr3_step_rate = 0
    attr3 = attr3_name + struct.pack("<IIIIII", attr3_index, attr3_format, attr3_offset, attr3_slot, attr3_slot_type, attr3_step_rate)
    
    # Vertex data: 3 vertices
    # pos (12), norm (12), uv (8)
    v0 = struct.pack("<ffffffff", 0.0, 0.0, 0.0,  0.0, 0.0, 1.0,  0.0, 0.0)
    v1 = struct.pack("<ffffffff", 1.0, 0.0, 0.0,  0.0, 0.0, 1.0,  1.0, 0.0)
    v2 = struct.pack("<ffffffff", 0.0, 1.0, 0.0,  0.0, 0.0, 1.0,  0.0, 1.0)
    vb_data = v0 + v1 + v2
    
    vb_header = struct.pack("<III", vertex_count, vertex_size, attr_count)
    vb_block = vb_header + attr1 + attr2 + attr3 + struct.pack("<I", len(vb_data)) + vb_data
    if len(vb_block) % 4 != 0:
        vb_block += b"\0" * (4 - (len(vb_block) % 4))
        
    # Index Buffer
    ib_count = 1
    index_count = 3
    index_size = 4
    ib_data = struct.pack("<III", 0, 1, 2)
    ib_header = struct.pack("<III", index_count, index_size, 0)  # unk_count = 0
    ib_block = ib_header + struct.pack("<I", len(ib_data)) + ib_data
    if len(ib_block) % 4 != 0:
        ib_block += b"\0" * (4 - (len(ib_block) % 4))
        
    return struct.pack("<I", vb_count) + vb_block + struct.pack("<I", ib_count) + ib_block

def make_vmdl_c():
    vbib = make_vbib_block()
    
    # File Header
    header_version = 12
    version = 1
    block_offset_field = 8
    block_count = 1
    
    # Block entry (12 bytes)
    b_type = b"VBIB"
    b_rel_off = 8
    b_size = len(vbib)
    block_entry = b_type + struct.pack("<II", b_rel_off, b_size)
    
    header = struct.pack("<IHHII", 0, header_version, version, block_offset_field, block_count)
    vmdl_data = header + block_entry + vbib
    
    # Update file size in header
    vmdl_data = struct.pack("<I", len(vmdl_data)) + vmdl_data[4:]
    return vmdl_data

def create_mock_vpk(dest_path):
    vmdl_c_data = make_vmdl_c()
    crc32_val = zlib.crc32(vmdl_c_data) & 0xFFFFFFFF
    
    # Directory entry mapping: agents/models/ctm_sas/ctm_sas.vmdl_c
    preload_bytes = 0
    archive_index = 0x7FFF  # VPK_EMBEDDED
    entry_offset = 0
    entry_length = len(vmdl_c_data)
    
    raw_dir_entry = struct.pack("<IHHII", crc32_val, preload_bytes, archive_index, entry_offset, entry_length)
    
    # Build tree
    # ext + \0 + path + \0 + filename + \0 + RawDirEntry + \xFF\xFF + \0 + \0 + \0
    tree_data = (
        b"vmdl_c\0" +
        b"agents/models/ctm_sas\0" +
        b"ctm_sas\0" +
        raw_dir_entry +
        b"\xFF\xFF" +
        b"\0\0\0"
    )
    
    # VPK Header (Version 2, 28 bytes)
    signature = 0x55AA1234
    version = 2
    tree_size = len(tree_data)
    file_data_section_size = len(vmdl_c_data)
    archive_md5_section_size = 0
    other_md5_section_size = 0
    signature_section_size = 0
    
    vpk_header = struct.pack("<IIIIIII", signature, version, tree_size, file_data_section_size, archive_md5_section_size, other_md5_section_size, signature_section_size)
    
    vpk_content = vpk_header + tree_data + vmdl_c_data
    
    os.makedirs(os.path.dirname(os.path.abspath(dest_path)), exist_ok=True)
    with open(dest_path, "wb") as f:
        f.write(vpk_content)

if __name__ == "__main__":
    create_mock_vpk("pak01_dir.vpk")

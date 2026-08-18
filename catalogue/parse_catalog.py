#!/usr/bin/env python3

import gzip
import math
import sqlite3
import struct
import sys
from pathlib import Path


# ============================================================
# Configuration
# ============================================================

INPUT_FILE = Path("catalog-3.23.dat")
OUTPUT_DB = Path("telescopehub_catalogue.db")

BATCH_SIZE = 5000


# ============================================================
# Binary reader
# ============================================================

class Reader:
    def __init__(self, data: bytes):
        self.data = data
        self.pos = 0

    def remaining(self) -> int:
        return len(self.data) - self.pos

    def read(self, size: int) -> bytes:
        if self.pos + size > len(self.data):
            raise EOFError(
                f"Unexpected end of data at offset {self.pos}"
            )

        result = self.data[
            self.pos:self.pos + size
        ]

        self.pos += size

        return result

    def uint32(self) -> int:
        return struct.unpack(
            ">I",
            self.read(4)
        )[0]

    def int32(self) -> int:
        return struct.unpack(
            ">i",
            self.read(4)
        )[0]

    def float32(self) -> float:
        return struct.unpack(
            ">d",
            self.read(8)
        )[0]

    def qstring(self) -> str:
        """
        Qt QDataStream QString representation.

        Qt writes:
            quint32 byte_length
            UTF-16BE code units
        """

        length = self.uint32()

        # Qt uses 0xFFFFFFFF for a null QString.
        if length == 0xFFFFFFFF:
            return ""

        if length == 0:
            return ""

        if length % 2 != 0:
            raise ValueError(
                f"Invalid QString byte length {length} "
                f"at offset {self.pos}"
            )

        raw = self.read(length)

        return raw.decode(
            "utf-16-be",
            errors="replace"
        )


# ============================================================
# Record reader
# ============================================================

def read_record(reader: Reader):
    """
    Read one Stellarium DSO catalogue record.

    This ordering matches NebulaMgr::convertDSOCatalog()
    and Nebula::readDSO() from Stellarium v3.23.
    """

    object_id = reader.uint32()

    ra_rad = reader.float32()
    dec_rad = reader.float32()

    b_mag = reader.float32()
    v_mag = reader.float32()

    object_type = reader.uint32()

    morphology = reader.qstring()

    major_axis_deg = reader.float32()
    minor_axis_deg = reader.float32()

    orientation_deg = reader.int32()

    redshift = reader.float32()
    redshift_error = reader.float32()

    parallax_mas = reader.float32()
    parallax_error_mas = reader.float32()

    distance = reader.float32()
    distance_error = reader.float32()

    # --------------------------------------------------------
    # Catalogue identifiers
    # --------------------------------------------------------

    ngc = reader.int32()
    ic = reader.int32()
    messier = reader.int32()
    caldwell = reader.int32()
    barnard = reader.int32()
    sh2 = reader.int32()
    vdb = reader.int32()
    rcw = reader.int32()
    ldn = reader.int32()
    lbn = reader.int32()
    cr = reader.int32()
    mel = reader.int32()
    pgc = reader.int32()
    ugc = reader.int32()

    ced = reader.qstring()

    arp = reader.int32()
    vv = reader.int32()

    pk = reader.qstring()
    png = reader.qstring()
    snrg = reader.qstring()
    aco = reader.qstring()
    hcg = reader.qstring()
    eso = reader.qstring()
    vdbh = reader.qstring()

    dwb = reader.int32()
    tr = reader.int32()
    st = reader.int32()
    ru = reader.int32()
    vdbha = reader.int32()

    # --------------------------------------------------------
    # Convert radians → degrees
    # --------------------------------------------------------

    ra_deg = (
        ra_rad *
        180.0 /
        math.pi
    )

    dec_deg = (
        dec_rad *
        180.0 /
        math.pi
    )

    return {
        "id": object_id,

        "ra_deg": ra_deg,
        "dec_deg": dec_deg,

        "b_mag": b_mag,
        "v_mag": v_mag,

        "object_type": object_type,
        "morphology": morphology,

        "major_axis_deg": major_axis_deg,
        "minor_axis_deg": minor_axis_deg,

        "orientation_deg": orientation_deg,

        "redshift": redshift,
        "redshift_error": redshift_error,

        "parallax_mas": parallax_mas,
        "parallax_error_mas": parallax_error_mas,

        "distance": distance,
        "distance_error": distance_error,

        "ngc": ngc,
        "ic": ic,
        "messier": messier,
        "caldwell": caldwell,
        "barnard": barnard,
        "sh2": sh2,
        "vdb": vdb,
        "rcw": rcw,
        "ldn": ldn,
        "lbn": lbn,
        "cr": cr,
        "mel": mel,
        "pgc": pgc,
        "ugc": ugc,

        "ced": ced,

        "arp": arp,
        "vv": vv,

        "pk": pk,
        "png": png,
        "snrg": snrg,
        "aco": aco,
        "hcg": hcg,
        "eso": eso,
        "vdbh": vdbh,

        "dwb": dwb,
        "tr": tr,
        "st": st,
        "ru": ru,
        "vdbha": vdbha,
    }


# ============================================================
# Human-readable designation
# ============================================================

def designation(record):
    """
    Choose a useful primary designation.
    """

    if record["messier"] > 0:
        return f"M {record['messier']}"

    if record["ngc"] > 0:
        return f"NGC {record['ngc']}"

    if record["ic"] > 0:
        return f"IC {record['ic']}"

    if record["caldwell"] > 0:
        return f"Caldwell {record['caldwell']}"

    if record["ugc"] > 0:
        return f"UGC {record['ugc']}"

    if record["pgc"] > 0:
        return f"PGC {record['pgc']}"

    if record["arp"] > 0:
        return f"Arp {record['arp']}"

    if record["barnard"] > 0:
        return f"B {record['barnard']}"

    return f"DSO {record['id']}"


# ============================================================
# Database
# ============================================================

def create_database(path: Path):
    if path.exists():
        print(
            f"Removing existing database: {path}"
        )

        path.unlink()

    connection = sqlite3.connect(
        path
    )

    cursor = connection.cursor()

    cursor.execute(
        """
        CREATE TABLE objects (
            id INTEGER PRIMARY KEY,

            designation TEXT NOT NULL,

            ra_deg REAL NOT NULL,
            dec_deg REAL NOT NULL,

            b_mag REAL,
            v_mag REAL,

            object_type INTEGER,
            morphology TEXT,

            major_axis_deg REAL,
            minor_axis_deg REAL,

            orientation_deg INTEGER,

            redshift REAL,
            redshift_error REAL,

            parallax_mas REAL,
            parallax_error_mas REAL,

            distance REAL,
            distance_error REAL,

            ngc INTEGER,
            ic INTEGER,
            messier INTEGER,
            caldwell INTEGER,
            barnard INTEGER,
            sh2 INTEGER,
            vdb INTEGER,
            rcw INTEGER,
            ldn INTEGER,
            lbn INTEGER,
            cr INTEGER,
            mel INTEGER,
            pgc INTEGER,
            ugc INTEGER,

            ced TEXT,

            arp INTEGER,
            vv INTEGER,

            pk TEXT,
            png TEXT,
            snrg TEXT,
            aco TEXT,
            hcg TEXT,
            eso TEXT,
            vdbh TEXT,

            dwb INTEGER,
            tr INTEGER,
            st INTEGER,
            ru INTEGER,
            vdbha INTEGER
        )
        """
    )

    # --------------------------------------------------------
    # Indexes useful to TelescopeHub
    # --------------------------------------------------------

    cursor.execute(
        """
        CREATE INDEX idx_ra_dec
        ON objects(ra_deg, dec_deg)
        """
    )

    cursor.execute(
        """
        CREATE INDEX idx_vmag
        ON objects(v_mag)
        """
    )

    cursor.execute(
        """
        CREATE INDEX idx_messier
        ON objects(messier)
        """
    )

    cursor.execute(
        """
        CREATE INDEX idx_ngc
        ON objects(ngc)
        """
    )

    cursor.execute(
        """
        CREATE INDEX idx_ic
        ON objects(ic)
        """
    )

    connection.commit()

    return connection


# ============================================================
# Insert
# ============================================================

INSERT_SQL = """
INSERT INTO objects (
    id,
    designation,
    ra_deg,
    dec_deg,
    b_mag,
    v_mag,
    object_type,
    morphology,
    major_axis_deg,
    minor_axis_deg,
    orientation_deg,
    redshift,
    redshift_error,
    parallax_mas,
    parallax_error_mas,
    distance,
    distance_error,
    ngc,
    ic,
    messier,
    caldwell,
    barnard,
    sh2,
    vdb,
    rcw,
    ldn,
    lbn,
    cr,
    mel,
    pgc,
    ugc,
    ced,
    arp,
    vv,
    pk,
    png,
    snrg,
    aco,
    hcg,
    eso,
    vdbh,
    dwb,
    tr,
    st,
    ru,
    vdbha
)
VALUES (
    {}
)
""".format(
    ", ".join(
        ["?"] * 46
    )
)


def record_to_tuple(record):
    return (
        record["id"],
        designation(record),

        record["ra_deg"],
        record["dec_deg"],

        record["b_mag"],
        record["v_mag"],

        record["object_type"],
        record["morphology"],

        record["major_axis_deg"],
        record["minor_axis_deg"],

        record["orientation_deg"],

        record["redshift"],
        record["redshift_error"],

        record["parallax_mas"],
        record["parallax_error_mas"],

        record["distance"],
        record["distance_error"],

        record["ngc"],
        record["ic"],
        record["messier"],
        record["caldwell"],
        record["barnard"],
        record["sh2"],
        record["vdb"],
        record["rcw"],
        record["ldn"],
        record["lbn"],
        record["cr"],
        record["mel"],
        record["pgc"],
        record["ugc"],

        record["ced"],

        record["arp"],
        record["vv"],

        record["pk"],
        record["png"],
        record["snrg"],
        record["aco"],
        record["hcg"],
        record["eso"],
        record["vdbh"],

        record["dwb"],
        record["tr"],
        record["st"],
        record["ru"],
        record["vdbha"],
    )


# ============================================================
# Parse
# ============================================================

def parse_catalogue(
    input_file: Path,
    output_db: Path
):
    print(
        f"Reading: {input_file}"
    )

    print(
        "Decompressing catalogue..."
    )

    with gzip.open(
        input_file,
        "rb"
    ) as archive:

        data = archive.read()

    print(
        f"Decompressed: {len(data):,} bytes"
    )

    reader = Reader(data)


    # ========================================================
    # Header
    # ========================================================

    version = reader.qstring()

    edition = reader.qstring()


    print(
        f"Catalogue version: {version}"
    )

    print(
        f"Edition: {edition}"
    )


    if version != "3.23":
        raise RuntimeError(
            f"Unexpected catalogue version: "
            f"{version!r}"
        )


    # ========================================================
    # Database
    # ========================================================

    connection = create_database(  output_db )

    cursor = connection.cursor()


    # ========================================================
    # Records
    # ========================================================

    count =  0

    batch = []


    try:
        while reader.remaining() > 0:

            record = read_record( reader )


            batch.append( record_to_tuple(
                    record
                )
            )


            count += 1


            if len(batch) >= BATCH_SIZE:

                cursor.executemany(
                    INSERT_SQL,
                    batch
                )

                connection.commit()

                batch.clear()


            if count % 50000 == 0:

                print(
                    f"Parsed {count:,} records..."
                )


        if batch:

            cursor.executemany(
                INSERT_SQL,
                batch
            )

            connection.commit()


    except EOFError as exc:

        print(
            f"\nParser stopped at record {count}:"
        )

        raise


    finally:
        connection.close()


    print()
    print(
        f"Finished. Parsed {count:,} objects."
    )

    print(
        f"Database: {output_db}"
    )


# ============================================================
# Test database
# ============================================================

def test_database(
    database: Path
):
    connection = sqlite3.connect(
            database
        )

    cursor = connection.cursor()


    print()
    print(
        "Database test:"
    )


    cursor.execute(
        "SELECT COUNT(*) FROM objects"
    )

    count = cursor.fetchone()[0]


    print(
        f"Objects: {count:,}"
    )


    # --------------------------------------------------------
    # Messier objects
    # --------------------------------------------------------

    print()
    print(
        "Messier objects:"
    )


    cursor.execute(
        """
        SELECT
            designation,
            ra_deg,
            dec_deg,
            v_mag,
            morphology
        FROM objects
        WHERE messier > 0
        ORDER BY messier
        LIMIT 10
        """
    )


    for row in cursor.fetchall():

        print(
            f"  {row[0]:12s} "
            f"RA={row[1]:9.4f} "
            f"Dec={row[2]:9.4f} "
            f"V={row[3]:6.2f} "
            f"{row[4]}"
        )


    # --------------------------------------------------------
    # Known objects
    # --------------------------------------------------------

    print()
    print(
        "Known-object checks:"
    )


    for messier_number in (
        31,
        42,
        45,
        13,
    ):
        cursor.execute(
            """
            SELECT
                designation,
                ra_deg,
                dec_deg,
                v_mag,
                morphology
            FROM objects
            WHERE messier = ?
            LIMIT 1
            """,
            (
                messier_number,
            )
        )


        row = cursor.fetchone()


        if row:
            print(
                f"  {row[0]}: "
                f"RA={row[1]:.6f}°, "
                f"Dec={row[2]:.6f}°, "
                f"V={row[3]:.2f}, "
                f"type={row[4]}"
            )
        else:
            print(
                f"  M {messier_number}: NOT FOUND"
            )


    connection.close()


# ============================================================
# Main
# ============================================================

def main():
    input_file = INPUT_FILE

    output_db =  OUTPUT_DB


    if len(sys.argv) >= 2:
        input_file = Path(
                sys.argv[1]
            )


    if len(sys.argv) >= 3:
        output_db =Path(
                sys.argv[2]
            )


    if not input_file.exists():

        print(
            f"ERROR: File not found: "
            f"{input_file}"
        )

        return 1


    try:

        parse_catalogue(
            input_file,
            output_db
        )


        test_database(
            output_db
        )


    except Exception as exc:

        print(
            f"\nERROR: {exc}"
        )

        return 1


    return 0


if __name__ == "__main__":
    raise SystemExit(
        main()
    )
#!/usr/bin/env python3

import re
import sqlite3
from pathlib import Path


NAMES_FILE = Path("names.dat")
DATABASE_FILE = Path("telescopehub_catalogue.db")


# ============================================================
# Catalogue columns
# ============================================================

CATALOGUE_COLUMNS = {
    "NGC": "ngc",
    "IC": "ic",
    "M": "messier",
    "C": "caldwell",
    "B": "barnard",
    "SH2": "sh2",
    "VDB": "vdb",
    "RCW": "rcw",
    "LDN": "ldn",
    "LBN": "lbn",
    "CR": "cr",
    "MEL": "mel",
    "PGC": "pgc",
    "UGC": "ugc",
    "ARP": "arp",
    "VV": "vv",
    "DWB": "dwb",
    "TR": "tr",
    "ST": "st",
    "RU": "ru",
    "VDBHA": "vdbha",
}


# ============================================================
# Fixed-width parser
# ============================================================

def parse_name_record(line: str):
    """
    Stellarium names.dat format:

        columns 1-5    catalogue prefix
        columns 6-20   catalogue identifier
        columns 21+    proper name

    Example:

        NGC  224             _("Andromeda Galaxy") # ...
    """

    if not line:
        return None

    if line.startswith("#"):
        return None

    line = line.rstrip("\r\n")

    if not line.strip():
        return None

    prefix = line[0:5].strip().upper()
    identifier = line[5:20].strip().upper()
    remainder = line[20:].strip()

    if not prefix or not identifier:
        return None

    # --------------------------------------------------------
    # Remove _("...") wrapper
    # --------------------------------------------------------

    match = re.search(
        r'_?\("([^"]*)"\)',
        remainder
    )

    if match:
        name = match.group(1).strip()
    else:
        # Fallback in case a future format differs.
        name = remainder

    # --------------------------------------------------------
    # Remove optional source comment
    # --------------------------------------------------------

    if "#" in name:
        name = name.split(
            "#",
            1
        )[0].rstrip()

    # Strip surrounding quotes if necessary.
    name = name.strip(
        '"'
    ).strip()

    if not name:
        return None

    return (
        prefix,
        identifier,
        name
    )


# ============================================================
# Resolve object ID
# ============================================================

def find_object(
    cursor,
    prefix: str,
    identifier: str
):
    """
    Map the names.dat catalogue reference onto our objects table.
    """

    column = CATALOGUE_COLUMNS.get(
        prefix
    )

    if column is None:
        return None

    # Numeric catalogues
    numeric_columns = {
        "ngc", "ic", "messier", "caldwell",
        "barnard", "sh2", "vdb", "rcw",
        "ldn", "lbn", "cr", "mel", "pgc",
        "ugc", "arp", "vv", "dwb", "tr",
        "st", "ru", "vdbha"
    }

    if column in numeric_columns:

        try:
            number = int(
                identifier
            )
        except ValueError:
            return None

        query = f"""
            SELECT id
            FROM objects
            WHERE {column} = ?
            LIMIT 1
        """

        cursor.execute(
            query,
            (number,)
        )

        row = cursor.fetchone()

        if row:
            return row[0]

        return None

    return None


# ============================================================
# Import names
# ============================================================

def import_names():
    if not NAMES_FILE.exists():
        raise FileNotFoundError(
            f"Names file not found: {NAMES_FILE}"
        )

    if not DATABASE_FILE.exists():
        raise FileNotFoundError(
            f"Database not found: {DATABASE_FILE}"
        )

    connection = sqlite3.connect(
        DATABASE_FILE
    )

    cursor = connection.cursor()

    print(
        f"Opening database: {DATABASE_FILE}"
    )


    # --------------------------------------------------------
    # Create names table
    # --------------------------------------------------------

    cursor.execute(
        """
        CREATE TABLE IF NOT EXISTS names (
            id INTEGER PRIMARY KEY AUTOINCREMENT,

            object_id INTEGER NOT NULL,

            catalogue TEXT NOT NULL,
            identifier TEXT NOT NULL,

            name TEXT NOT NULL,

            UNIQUE (
                object_id,
                catalogue,
                identifier,
                name
            ),

            FOREIGN KEY (
                object_id
            )
            REFERENCES objects(id)
        )
        """
    )


    cursor.execute(
        """
        CREATE INDEX IF NOT EXISTS
        idx_names_object
        ON names(object_id)
        """
    )


    cursor.execute(
        """
        CREATE INDEX IF NOT EXISTS
        idx_names_name
        ON names(name)
        """
    )


    connection.commit()


    # --------------------------------------------------------
    # Read names.dat
    # --------------------------------------------------------

    total = 0

    imported = 0

    unresolved = 0

    duplicates = 0


    with NAMES_FILE.open(
        "r",
        encoding="utf-8"
    ) as source:

        for line in source:

            parsed = parse_name_record(
                    line
                )

            if parsed is None:
                continue

            prefix, identifier, name = parsed

            total += 1


            object_id = find_object(
                    cursor,
                    prefix,
                    identifier
                )


            if object_id is None:

                unresolved += 1

                continue


            try:

                cursor.execute(
                    """
                    INSERT INTO names (
                        object_id,
                        catalogue,
                        identifier,
                        name
                    )
                    VALUES (?, ?, ?, ?)
                    """,
                    (
                        object_id,
                        prefix,
                        identifier,
                        name
                    )
                )

                imported += 1

            except sqlite3.IntegrityError:

                duplicates += 1


            if total % 100 == 0:

                connection.commit()

                print(
                    f"Processed {total:,} "
                    f"names..."
                )


    connection.commit()


    # ========================================================
    # Summary
    # ========================================================

    cursor.execute(
        "SELECT COUNT(*) FROM names"
    )

    database_names = cursor.fetchone()[0]


    cursor.execute(
        "SELECT COUNT(DISTINCT object_id) FROM names"
    )

    named_objects = cursor.fetchone()[0]


    print()
    print(
        "========================================"
    )
    print(
        "NAME IMPORT COMPLETE"
    )
    print(
        "========================================"
    )

    print(
        f"Records found:      {total:,}"
    )

    print(
        f"Names imported:     {imported:,}"
    )

    print(
        f"Duplicates skipped: {duplicates:,}"
    )

    print(
        f"Unresolved:         {unresolved:,}"
    )

    print(
        f"Names in database:  {database_names:,}"
    )

    print(
        f"Named objects:      {named_objects:,}"
    )


    # ========================================================
    # Demonstration
    # ========================================================

    print()
    print(
        "Example names:"
    )


    cursor.execute(
        """
        SELECT
            o.designation,
            n.catalogue,
            n.identifier,
            n.name
        FROM names n
        JOIN objects o
            ON o.id = n.object_id
        WHERE o.messier IN (31, 42, 45)
        ORDER BY o.messier, n.name
        LIMIT 30
        """
    )


    for row in cursor.fetchall():

        print(
            f"  {row[0]:8s} "
            f"{row[3]}"
        )


    connection.close()


# ============================================================
# Main
# ============================================================

if __name__ == "__main__":
    import_names()
#pragma once

#include <QByteArray>
#include <QFile>
#include <QJsonObject>
#include <QList>
#include <QString>

#include <cstdint>

// Dafin: a strict, bounded reader for the zip inside a Filmora project (.wfp), and only for that.
//
// The general reader (core/base/ZipArchive) is lenient on purpose — the other formats Dafin opens
// are small and trusted — and it is shared by eight other readers, so it is left as it is until
// each of them has been looked at (docs/filmora-wfp-safe.md lists them). A .wfp is different: the
// start screen finds every one on the disk and reads it without being asked, and Open reads it on
// the GUI thread. So this reader refuses anything it cannot fully check:
//
//   - one end-of-central-directory record that ends exactly at the end of the file; no ZIP64, no
//     multi-disk archive; central directory in range, exactly as long as it says;
//   - every member name checked and canonicalised (slashes, case) and no two members the same
//     after that; no absolute, drive-lettered, "..", control-character or oversized name;
//   - every local header agreeing with the central one (name, method, and, unless the local
//     header defers to a data descriptor, size and CRC); member data inside the file, before the
//     central directory, and no two members overlapping;
//   - sizes are read as 64-bit, checked against the limits and against what deflate can possibly
//     produce BEFORE anything is allocated;
//   - a member is only handed out after it inflated to exactly its declared size, ended with the
//     deflate stream's end marker with no input left over, and matched its CRC-32;
//   - the bytes inflated over the archive's life stay under one budget;
//   - JSON is size-limited (by the member limit), depth-limited and must be an object.
//
// Nothing is ever extracted to disk: members are read into memory, and only by name.
namespace drift::interchange::filmora {

struct WfpLimits
{
    qint64 maxArchiveBytes = qint64(2) << 30;       // the .wfp file itself (no ZIP64, so < 4 GiB)
    int maxEntries = 20000;                          // central directory records
    qint64 maxCentralDirectoryBytes = qint64(8) << 20;
    int maxNameBytes = 1024;                         // one member name, as stored
    qint64 maxMemberBytes = qint64(32) << 20;        // one member, inflated
    qint64 maxTotalInflatedBytes = qint64(64) << 20; // every member read through one WfpArchive
    int maxJsonDepth = 128;                          // nesting of [ and {
};

// Why a read was refused. The key() strings are stable (tests and logs use them); message() is
// what the person sees.
enum class WfpError {
    None,
    CannotOpen,
    ArchiveTooLarge,
    NotZip,
    MultiDisk,
    Zip64Unsupported,
    TooManyEntries,
    CentralDirectoryInvalid,
    UnsafeMemberPath,
    DuplicateMember,
    LocalHeaderInvalid,
    LocalNameMismatch,
    LocalMetadataMismatch,
    OverlappingMembers,
    MemberMissing,
    UnsupportedMethod,
    EncryptedMember,
    DeclaredSizeTooLarge, // declared size over the limit, or more than deflate can produce
    SizeInconsistent,     // stored member whose two sizes differ
    TotalInflationLimit,
    ReadFailed,
    IncompleteStream,     // the compressed data ends before the deflate stream does
    CorruptStream,
    SizeMismatch,         // inflated size is not the declared size, or input is left over
    CrcMismatch,
    JsonTooDeep,
    JsonInvalid,
    JsonNotObject,
};

struct WfpStatus
{
    WfpError code = WfpError::None;
    QString detail; // member name or other specifics, for logs; never shown as a path

    bool ok() const { return code == WfpError::None; }
    QString key() const;
    // One sentence for the person. Everything but a JSON syntax problem is a reason not to trust
    // the file at all (isIntegrityFailure()).
    QString message() const;
    // False for "the member is not there" and "its JSON does not parse": the file is intact and
    // just does not hold what was looked for. True for everything that says the zip is damaged,
    // forged or over a limit.
    bool isIntegrityFailure() const;
};

class WfpArchive
{
public:
    struct Member
    {
        QString name;          // as stored (decoded)
        QString canonicalName; // slashes forward, case folded, no trailing slash: the lookup key
        QByteArray rawName;
        quint16 flags = 0;
        quint16 method = 0;
        quint32 crc32 = 0;
        quint32 compressedSize = 0;
        quint32 uncompressedSize = 0;
        quint32 localOffset = 0;
        qint64 dataOffset = 0; // set by open() once the local header has been checked
        bool isDir = false;
    };

    explicit WfpArchive(const QString &path, const WfpLimits &limits = {});
    WfpArchive(const WfpArchive &) = delete;
    WfpArchive &operator=(const WfpArchive &) = delete;

    // Reads and checks the whole directory and every local header (not the member data).
    bool open(WfpStatus *status);

    const QList<Member> &members() const { return m_members; }
    // By name, ignoring case and slash direction. Unique, because duplicates are refused.
    const Member *find(const QString &name) const;

    // The member's verified bytes (see the list above).
    bool read(const Member &member, QByteArray *out, WfpStatus *status);
    // A verified member as a JSON object: BOM stripped, depth-limited.
    bool readJsonObject(const Member &member, QJsonObject *out, WfpStatus *status);
    // Inflates and checks every member without keeping the data — for tests and diagnostics, not
    // for opening a project (which only needs two or three members).
    bool verifyAll(WfpStatus *status);

    qint64 inflatedBytes() const { return m_inflatedTotal; }
    const WfpLimits &limits() const { return m_limits; }

    // The canonical form find() looks up by; empty when the name is not a safe relative path.
    static QString canonicalName(const QString &name);

private:
    bool load(WfpStatus *status);
    bool inflateMember(const Member &member, QByteArray *out, WfpStatus *status);

    QString m_path;
    WfpLimits m_limits;
    QFile m_file;
    QList<Member> m_members;
    qint64 m_inflatedTotal = 0;
    bool m_opened = false;
};

} // namespace drift::interchange::filmora

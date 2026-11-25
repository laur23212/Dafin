#include "core/interchange/filmora/WfpArchive.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSet>
#include <QStringDecoder>

#include <zlib.h>

#include <algorithm>
#include <cstring>

namespace drift::interchange::filmora {

namespace {

constexpr quint32 kLocalMagic = 0x04034b50;
constexpr quint32 kCentralMagic = 0x02014b50;
constexpr quint32 kEocdMagic = 0x06054b50;
constexpr quint32 kZip64LocatorMagic = 0x07064b50;
constexpr quint16 kFlagEncrypted = 0x0001;
constexpr quint16 kFlagDataDescriptor = 0x0008;
constexpr quint16 kFlagStrongEncryption = 0x0040;
constexpr quint16 kFlagMaskedHeaders = 0x2000;
constexpr quint32 kMax32 = 0xFFFFFFFFu;
constexpr int kChunk = 64 * 1024;

QString tr(const char *text)
{
    return QCoreApplication::translate("FilmoraImport", text);
}

quint16 u16(const char *p)
{
    const auto *b = reinterpret_cast<const uchar *>(p);
    return quint16(b[0] | (b[1] << 8));
}

quint32 u32(const char *p)
{
    const auto *b = reinterpret_cast<const uchar *>(p);
    return quint32(quint32(b[0]) | (quint32(b[1]) << 8) | (quint32(b[2]) << 16) | (quint32(b[3]) << 24));
}

bool fail(WfpStatus *status, WfpError code, const QString &detail = {})
{
    if (status) {
        status->code = code;
        status->detail = detail;
    }
    return false;
}

// UTF-8 when the bytes are valid UTF-8, else Latin-1 (zip's original code page is close enough
// for names, and keeps different byte strings different, so two odd names are not merged into
// one by replacement characters).
QString decodeName(const QByteArray &raw)
{
    QStringDecoder decoder(QStringConverter::Utf8, QStringConverter::Flag::Stateless);
    const QString s = decoder(raw);
    if (!decoder.hasError())
        return s;
    return QString::fromLatin1(raw);
}

// The most deflate can expand input: 1032:1 for long runs. A declared size beyond this for the
// compressed size is a lie, whatever the limits are. Generous constants on purpose: this is a
// sanity bound, not the policy.
bool deflateSizeIsPossible(quint32 compressed, quint32 uncompressed)
{
    return qint64(uncompressed) <= qint64(compressed) * 1100 + 4096;
}

bool jsonDepthWithin(const QByteArray &data, int maxDepth)
{
    int depth = 0;
    bool inString = false;
    bool escaped = false;
    for (const char ch : data) {
        if (inString) {
            if (escaped)
                escaped = false;
            else if (ch == '\\')
                escaped = true;
            else if (ch == '"')
                inString = false;
            continue;
        }
        if (ch == '"') {
            inString = true;
        } else if (ch == '[' || ch == '{') {
            if (++depth > maxDepth)
                return false;
        } else if (ch == ']' || ch == '}') {
            if (depth > 0)
                --depth;
        }
    }
    return true;
}

uLong crcOf(const QByteArray &data)
{
    uLong crc = crc32(0L, Z_NULL, 0);
    qsizetype offset = 0;
    while (offset < data.size()) {
        const uInt n = uInt(std::min<qsizetype>(data.size() - offset, 1 << 24));
        crc = crc32(crc, reinterpret_cast<const Bytef *>(data.constData() + offset), n);
        offset += n;
    }
    return crc;
}

} // namespace

// --- WfpStatus ----------------------------------------------------------------------------------

QString WfpStatus::key() const
{
    switch (code) {
    case WfpError::None:
        return QStringLiteral("none");
    case WfpError::CannotOpen:
        return QStringLiteral("cannot_open");
    case WfpError::ArchiveTooLarge:
        return QStringLiteral("archive_too_large");
    case WfpError::NotZip:
        return QStringLiteral("not_zip");
    case WfpError::MultiDisk:
        return QStringLiteral("multi_disk");
    case WfpError::Zip64Unsupported:
        return QStringLiteral("zip64_unsupported");
    case WfpError::TooManyEntries:
        return QStringLiteral("too_many_entries");
    case WfpError::CentralDirectoryInvalid:
        return QStringLiteral("central_directory_invalid");
    case WfpError::UnsafeMemberPath:
        return QStringLiteral("unsafe_member_path");
    case WfpError::DuplicateMember:
        return QStringLiteral("duplicate_casefold_member");
    case WfpError::LocalHeaderInvalid:
        return QStringLiteral("local_header_invalid");
    case WfpError::LocalNameMismatch:
        return QStringLiteral("local_central_name_mismatch");
    case WfpError::LocalMetadataMismatch:
        return QStringLiteral("local_central_metadata_mismatch");
    case WfpError::OverlappingMembers:
        return QStringLiteral("overlapping_members");
    case WfpError::MemberMissing:
        return QStringLiteral("member_missing");
    case WfpError::UnsupportedMethod:
        return QStringLiteral("unsupported_method");
    case WfpError::EncryptedMember:
        return QStringLiteral("encrypted_member");
    case WfpError::DeclaredSizeTooLarge:
        return QStringLiteral("declared_uncompressed_bytes");
    case WfpError::SizeInconsistent:
        return QStringLiteral("size_inconsistent");
    case WfpError::TotalInflationLimit:
        return QStringLiteral("total_inflation_limit");
    case WfpError::ReadFailed:
        return QStringLiteral("read_failed");
    case WfpError::IncompleteStream:
        return QStringLiteral("incomplete_deflate_stream");
    case WfpError::CorruptStream:
        return QStringLiteral("corrupt_deflate_stream");
    case WfpError::SizeMismatch:
        return QStringLiteral("size_mismatch");
    case WfpError::CrcMismatch:
        return QStringLiteral("crc32_mismatch");
    case WfpError::Cancelled:
        return QStringLiteral("cancelled");
    case WfpError::JsonTooDeep:
        return QStringLiteral("json_too_deep");
    case WfpError::JsonInvalid:
        return QStringLiteral("json_invalid");
    case WfpError::JsonNotObject:
        return QStringLiteral("json_not_object");
    }
    return QStringLiteral("unknown");
}

QString WfpStatus::message() const
{
    switch (code) {
    case WfpError::None:
        return {};
    case WfpError::CannotOpen:
        return tr("The file can't be opened.");
    case WfpError::ArchiveTooLarge:
        return tr("The file is larger than a Filmora project Dafin will read.");
    case WfpError::NotZip:
        return tr("The file is not a Filmora project (it is not a zip archive).");
    case WfpError::MultiDisk:
    case WfpError::Zip64Unsupported:
        return tr("The file uses zip features Dafin does not read in Filmora projects.");
    case WfpError::TooManyEntries:
        return tr("The file holds far more items than a Filmora project does.");
    case WfpError::CentralDirectoryInvalid:
    case WfpError::LocalHeaderInvalid:
        return tr("The file's contents list is damaged.");
    case WfpError::UnsafeMemberPath:
        return tr("The file holds an item with an unsafe name.");
    case WfpError::DuplicateMember:
        return tr("The file holds two items with the same name.");
    case WfpError::LocalNameMismatch:
    case WfpError::LocalMetadataMismatch:
    case WfpError::OverlappingMembers:
        return tr("The file's contents list does not match its contents.");
    case WfpError::MemberMissing:
        return tr("An item the project needs is not in the file.");
    case WfpError::UnsupportedMethod:
        return tr("The file compresses an item in a way Dafin does not read.");
    case WfpError::EncryptedMember:
        return tr("The file holds an encrypted item.");
    case WfpError::DeclaredSizeTooLarge:
    case WfpError::TotalInflationLimit:
        return tr("The file would expand to more data than a Filmora project holds.");
    case WfpError::SizeInconsistent:
    case WfpError::SizeMismatch:
        return tr("An item in the file is not the size the file says.");
    case WfpError::ReadFailed:
        return tr("The file could not be read to the end.");
    case WfpError::IncompleteStream:
        return tr("The file is cut short or damaged.");
    case WfpError::CorruptStream:
    case WfpError::CrcMismatch:
        return tr("The file is damaged (an item fails its checksum).");
    case WfpError::JsonTooDeep:
        return tr("A project description in the file is nested far deeper than Filmora writes.");
    case WfpError::JsonInvalid:
    case WfpError::JsonNotObject:
        return tr("A project description in the file can't be read.");
    case WfpError::Cancelled:
        return tr("The import was cancelled.");
    }
    return {};
}

bool WfpStatus::isIntegrityFailure() const
{
    switch (code) {
    case WfpError::None:
    case WfpError::MemberMissing:
    case WfpError::JsonInvalid:
    case WfpError::JsonNotObject:
        return false;
    default:
        return true;
    }
}

// --- WfpArchive ---------------------------------------------------------------------------------

WfpArchive::WfpArchive(const QString &path, const WfpLimits &limits)
    : m_path(path)
    , m_limits(limits)
    , m_file(path)
{
}

QString WfpArchive::canonicalName(const QString &name)
{
    if (name.isEmpty())
        return {};
    QString s = name;
    s.replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (s.startsWith(QLatin1Char('/')))
        return {};
    if (s.endsWith(QLatin1Char('/')))
        s.chop(1);
    if (s.isEmpty())
        return {};
    const QStringList parts = s.split(QLatin1Char('/'), Qt::KeepEmptyParts);
    for (const QString &part : parts) {
        if (part.isEmpty() || part == QLatin1String(".") || part == QLatin1String(".."))
            return {};
        // A drive letter ("C:"), an NTFS stream ("name:stream"): not for a name in a project.
        if (part.contains(QLatin1Char(':')))
            return {};
        for (const QChar c : part) {
            if (c.unicode() < 0x20 || c.unicode() == 0x7f)
                return {};
        }
    }
    return s.toCaseFolded();
}

bool WfpArchive::open(WfpStatus *status)
{
    m_members.clear();
    m_inflatedTotal = 0;
    m_opened = false;
    if (load(status))
        return true;
    // A half-read directory is never left to be used.
    m_members.clear();
    m_opened = false;
    return false;
}

bool WfpArchive::load(WfpStatus *status)
{
    if (m_file.isOpen())
        m_file.close();
    const QFileInfo info(m_path);
    if (!info.isFile() || !m_file.open(QIODevice::ReadOnly))
        return fail(status, WfpError::CannotOpen);
    const qint64 fileSize = m_file.size();
    if (fileSize < 22)
        return fail(status, WfpError::NotZip);
    if (fileSize > m_limits.maxArchiveBytes || fileSize >= qint64(kMax32))
        return fail(status, WfpError::ArchiveTooLarge);

    // The end-of-central-directory record: the last one that ends exactly at the end of the file.
    const qint64 scanLen = std::min<qint64>(fileSize, 65535 + 22);
    if (!m_file.seek(fileSize - scanLen))
        return fail(status, WfpError::ReadFailed);
    const QByteArray tail = m_file.read(scanLen);
    if (tail.size() != scanLen)
        return fail(status, WfpError::ReadFailed);
    qint64 eocdPos = -1;
    for (qint64 i = tail.size() - 22; i >= 0; --i) {
        if (u32(tail.constData() + i) != kEocdMagic)
            continue;
        const quint16 commentLen = u16(tail.constData() + i + 20);
        if (i + 22 + commentLen == tail.size()) {
            eocdPos = fileSize - scanLen + i;
            break;
        }
    }
    if (eocdPos < 0)
        return fail(status, WfpError::NotZip);
    const char *eocd = tail.constData() + (eocdPos - (fileSize - scanLen));

    // ZIP64 is announced by a locator right before the record, or by sentinel values in it.
    if (eocdPos >= 20) {
        if (!m_file.seek(eocdPos - 20))
            return fail(status, WfpError::ReadFailed);
        const QByteArray locator = m_file.read(4);
        if (locator.size() == 4 && u32(locator.constData()) == kZip64LocatorMagic)
            return fail(status, WfpError::Zip64Unsupported);
    }
    const quint16 diskNumber = u16(eocd + 4);
    const quint16 cdDisk = u16(eocd + 6);
    const quint16 entriesOnDisk = u16(eocd + 8);
    const quint16 totalEntries = u16(eocd + 10);
    const quint32 cdSize = u32(eocd + 12);
    const quint32 cdOffset = u32(eocd + 16);
    if (diskNumber != 0 || cdDisk != 0)
        return fail(status, WfpError::MultiDisk);
    if (totalEntries == 0xFFFF || cdSize == kMax32 || cdOffset == kMax32)
        return fail(status, WfpError::Zip64Unsupported);
    if (entriesOnDisk != totalEntries)
        return fail(status, WfpError::CentralDirectoryInvalid, QStringLiteral("entry counts differ"));
    if (totalEntries > m_limits.maxEntries)
        return fail(status, WfpError::TooManyEntries);
    if (cdSize > m_limits.maxCentralDirectoryBytes)
        return fail(status, WfpError::CentralDirectoryInvalid, QStringLiteral("directory too large"));
    if (qint64(cdOffset) + qint64(cdSize) != eocdPos)
        return fail(status, WfpError::CentralDirectoryInvalid, QStringLiteral("directory out of place"));

    if (!m_file.seek(cdOffset))
        return fail(status, WfpError::ReadFailed);
    const QByteArray cd = m_file.read(cdSize);
    if (cd.size() != qint64(cdSize))
        return fail(status, WfpError::ReadFailed);

    // --- central directory ---
    QSet<QString> seen;
    m_members.reserve(totalEntries);
    qint64 pos = 0;
    for (int i = 0; i < totalEntries; ++i) {
        if (pos + 46 > cd.size())
            return fail(status, WfpError::CentralDirectoryInvalid, QStringLiteral("record cut short"));
        const char *h = cd.constData() + pos;
        if (u32(h) != kCentralMagic)
            return fail(status, WfpError::CentralDirectoryInvalid, QStringLiteral("bad record signature"));
        Member m;
        m.flags = u16(h + 8);
        m.method = u16(h + 10);
        m.crc32 = u32(h + 16);
        m.compressedSize = u32(h + 20);
        m.uncompressedSize = u32(h + 24);
        const quint16 nameLen = u16(h + 28);
        const quint16 extraLen = u16(h + 30);
        const quint16 commentLen = u16(h + 32);
        const quint16 startDisk = u16(h + 34);
        m.localOffset = u32(h + 42);
        if (pos + 46 + nameLen + extraLen + commentLen > cd.size())
            return fail(status, WfpError::CentralDirectoryInvalid, QStringLiteral("record cut short"));
        if (startDisk != 0)
            return fail(status, WfpError::MultiDisk);
        if (m.compressedSize == kMax32 || m.uncompressedSize == kMax32 || m.localOffset == kMax32)
            return fail(status, WfpError::Zip64Unsupported);

        // The extra field must parse, and must not be ZIP64's.
        const char *extra = h + 46 + nameLen;
        qint64 e = 0;
        while (e < extraLen) {
            if (e + 4 > extraLen)
                return fail(status, WfpError::CentralDirectoryInvalid, QStringLiteral("extra field"));
            const quint16 id = u16(extra + e);
            const quint16 len = u16(extra + e + 2);
            if (e + 4 + len > extraLen)
                return fail(status, WfpError::CentralDirectoryInvalid, QStringLiteral("extra field"));
            if (id == 0x0001)
                return fail(status, WfpError::Zip64Unsupported);
            e += 4 + len;
        }

        if (nameLen == 0 || nameLen > m_limits.maxNameBytes)
            return fail(status, WfpError::UnsafeMemberPath, QStringLiteral("name length"));
        m.rawName = QByteArray(h + 46, nameLen);
        m.name = decodeName(m.rawName);
        m.isDir = m.name.endsWith(QLatin1Char('/')) || m.name.endsWith(QLatin1Char('\\'));
        m.canonicalName = canonicalName(m.name);
        if (m.canonicalName.isEmpty())
            return fail(status, WfpError::UnsafeMemberPath, m.name.left(80));
        if (seen.contains(m.canonicalName))
            return fail(status, WfpError::DuplicateMember, m.name.left(80));
        seen.insert(m.canonicalName);

        // Sizes that cannot be true, caught before anything is allocated for them.
        if (m.method == 0 && m.compressedSize != m.uncompressedSize)
            return fail(status, WfpError::SizeInconsistent, m.name.left(80));
        if (m.method == 8 && !deflateSizeIsPossible(m.compressedSize, m.uncompressedSize))
            return fail(status, WfpError::DeclaredSizeTooLarge, m.name.left(80));
        if (qint64(m.localOffset) + 30 > qint64(cdOffset))
            return fail(status, WfpError::LocalHeaderInvalid, m.name.left(80));

        m_members.append(m);
        pos += 46 + nameLen + extraLen + commentLen;
    }
    if (pos != cd.size())
        return fail(status, WfpError::CentralDirectoryInvalid, QStringLiteral("directory length"));

    // --- local headers ---
    for (Member &m : m_members) {
        if (!m_file.seek(m.localOffset))
            return fail(status, WfpError::ReadFailed);
        const QByteArray local = m_file.read(30);
        if (local.size() != 30 || u32(local.constData()) != kLocalMagic)
            return fail(status, WfpError::LocalHeaderInvalid, m.name.left(80));
        const quint16 localFlags = u16(local.constData() + 6);
        const quint16 localMethod = u16(local.constData() + 8);
        const quint16 localNameLen = u16(local.constData() + 26);
        const quint16 localExtraLen = u16(local.constData() + 28);
        if (qint64(m.localOffset) + 30 + localNameLen + localExtraLen > qint64(cdOffset))
            return fail(status, WfpError::LocalHeaderInvalid, m.name.left(80));
        const QByteArray localName = m_file.read(localNameLen);
        if (localName.size() != localNameLen)
            return fail(status, WfpError::ReadFailed);
        if (localName != m.rawName)
            return fail(status, WfpError::LocalNameMismatch, m.name.left(80));
        if (localMethod != m.method || (localFlags & kFlagEncrypted) != (m.flags & kFlagEncrypted))
            return fail(status, WfpError::LocalMetadataMismatch, m.name.left(80));
        if (!(localFlags & kFlagDataDescriptor)) {
            const quint32 lcrc = u32(local.constData() + 14);
            const quint32 lcomp = u32(local.constData() + 18);
            const quint32 luncomp = u32(local.constData() + 22);
            if (lcomp == kMax32 || luncomp == kMax32)
                return fail(status, WfpError::Zip64Unsupported);
            if (lcrc != m.crc32 || lcomp != m.compressedSize || luncomp != m.uncompressedSize)
                return fail(status, WfpError::LocalMetadataMismatch, m.name.left(80));
        }
        m.dataOffset = qint64(m.localOffset) + 30 + localNameLen + localExtraLen;
        if (m.dataOffset + qint64(m.compressedSize) > qint64(cdOffset))
            return fail(status, WfpError::LocalHeaderInvalid, m.name.left(80));
    }

    // No two members may overlap (the data of one inside the next).
    QList<const Member *> byOffset;
    byOffset.reserve(m_members.size());
    for (const Member &m : std::as_const(m_members))
        byOffset.append(&m);
    std::sort(byOffset.begin(), byOffset.end(),
              [](const Member *a, const Member *b) { return a->localOffset < b->localOffset; });
    for (qsizetype i = 0; i + 1 < byOffset.size(); ++i) {
        const Member *a = byOffset.at(i);
        const Member *b = byOffset.at(i + 1);
        if (a->localOffset == b->localOffset
            || a->dataOffset + qint64(a->compressedSize) > qint64(b->localOffset))
            return fail(status, WfpError::OverlappingMembers, a->name.left(80));
    }

    m_opened = true;
    if (status) {
        status->code = WfpError::None;
        status->detail.clear();
    }
    return true;
}

const WfpArchive::Member *WfpArchive::find(const QString &name) const
{
    const QString key = canonicalName(name);
    if (key.isEmpty())
        return nullptr;
    for (const Member &m : m_members) {
        if (m.canonicalName == key)
            return &m;
    }
    return nullptr;
}

bool WfpArchive::inflateMember(const Member &member, QByteArray *out, WfpStatus *status)
{
    if (!m_opened)
        return fail(status, WfpError::ReadFailed, QStringLiteral("archive not open"));
    if (member.isDir)
        return fail(status, WfpError::MemberMissing, member.name.left(80));
    if (member.flags & (kFlagEncrypted | kFlagStrongEncryption | kFlagMaskedHeaders))
        return fail(status, WfpError::EncryptedMember, member.name.left(80));
    if (member.method != 0 && member.method != 8)
        return fail(status, WfpError::UnsupportedMethod, member.name.left(80));
    const qint64 declared = member.uncompressedSize;
    if (declared > m_limits.maxMemberBytes)
        return fail(status, WfpError::DeclaredSizeTooLarge, member.name.left(80));
    if (m_inflatedTotal + declared > m_limits.maxTotalInflatedBytes)
        return fail(status, WfpError::TotalInflationLimit, member.name.left(80));
    if (!m_file.seek(member.dataOffset))
        return fail(status, WfpError::ReadFailed);

    QByteArray data;
    data.resize(qsizetype(declared)); // declared <= maxMemberBytes: checked above, nothing narrowed
    if (data.size() != declared)
        return fail(status, WfpError::DeclaredSizeTooLarge, member.name.left(80));

    if (member.method == 0) {
        qint64 done = 0;
        while (done < declared) {
            if (isCancelled(m_cancel)) {
                m_inflatedTotal += done;
                return fail(status, WfpError::Cancelled, member.name.left(80));
            }
            const qint64 n = std::min<qint64>(declared - done, 1 << 20);
            if (m_file.read(data.data() + done, n) != n) {
                m_inflatedTotal += done;
                return fail(status, WfpError::ReadFailed, member.name.left(80));
            }
            done += n;
        }
        m_inflatedTotal += done;
    } else if (member.compressedSize == 0) {
        // An empty member written as an empty deflate "stream" of no bytes at all.
        if (declared != 0)
            return fail(status, WfpError::IncompleteStream, member.name.left(80));
    } else {
        z_stream strm;
        std::memset(&strm, 0, sizeof(strm));
        if (inflateInit2(&strm, -MAX_WBITS) != Z_OK)
            return fail(status, WfpError::CorruptStream, QStringLiteral("zlib init"));

        QByteArray inBuf(kChunk, Qt::Uninitialized);
        char scratch[256];
        qint64 remaining = member.compressedSize;
        WfpError error = WfpError::None;
        bool ended = false;
        while (true) {
            if (isCancelled(m_cancel)) {
                error = WfpError::Cancelled;
                break;
            }
            if (strm.avail_in == 0 && remaining > 0) {
                const qint64 n = std::min<qint64>(remaining, kChunk);
                if (m_file.read(inBuf.data(), n) != n) {
                    error = WfpError::ReadFailed;
                    break;
                }
                strm.next_in = reinterpret_cast<Bytef *>(inBuf.data());
                strm.avail_in = uInt(n);
                remaining -= n;
            }
            const bool overflow = qint64(strm.total_out) >= declared;
            if (overflow) {
                strm.next_out = reinterpret_cast<Bytef *>(scratch);
                strm.avail_out = sizeof(scratch);
            } else {
                strm.next_out = reinterpret_cast<Bytef *>(data.data()) + strm.total_out;
                strm.avail_out = uInt(declared - qint64(strm.total_out));
            }
            const int ret = inflate(&strm, Z_NO_FLUSH);
            if (overflow && strm.avail_out != sizeof(scratch)) {
                error = WfpError::SizeMismatch; // more output than the member declares
                break;
            }
            if (ret == Z_STREAM_END) {
                ended = true;
                break;
            }
            if (ret == Z_OK)
                continue;
            if (ret == Z_BUF_ERROR && strm.avail_in == 0 && remaining == 0)
                error = WfpError::IncompleteStream;
            else
                error = WfpError::CorruptStream;
            break;
        }
        const qint64 produced = qint64(strm.total_out);
        const bool inputLeft = strm.avail_in != 0 || remaining != 0;
        inflateEnd(&strm);
        m_inflatedTotal += produced;
        if (error != WfpError::None)
            return fail(status, error, member.name.left(80));
        if (!ended || produced != declared || inputLeft)
            return fail(status, WfpError::SizeMismatch, member.name.left(80));
    }

    if (quint32(crcOf(data)) != member.crc32)
        return fail(status, WfpError::CrcMismatch, member.name.left(80));
    if (out)
        *out = std::move(data);
    return true;
}

bool WfpArchive::read(const Member &member, QByteArray *out, WfpStatus *status)
{
    const bool ok = inflateMember(member, out, status);
    if (ok && status) {
        status->code = WfpError::None;
        status->detail.clear();
    }
    return ok;
}

bool WfpArchive::readJsonObject(const Member &member, QJsonObject *out, WfpStatus *status)
{
    QByteArray data;
    if (!read(member, &data, status))
        return false;
    // Some writers put a byte-order mark first.
    if (data.startsWith("\xEF\xBB\xBF"))
        data.remove(0, 3);
    if (!jsonDepthWithin(data, m_limits.maxJsonDepth))
        return fail(status, WfpError::JsonTooDeep, member.name.left(80));
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError)
        return fail(status, WfpError::JsonInvalid, member.name.left(80));
    if (!doc.isObject())
        return fail(status, WfpError::JsonNotObject, member.name.left(80));
    if (out)
        *out = doc.object();
    return true;
}

bool WfpArchive::verifyAll(WfpStatus *status)
{
    for (const Member &m : std::as_const(m_members)) {
        if (m.isDir)
            continue;
        const qint64 before = m_inflatedTotal;
        const bool ok = inflateMember(m, nullptr, status);
        m_inflatedTotal = before; // checking is not reading: it does not use up the budget
        if (!ok)
            return false;
    }
    if (status) {
        status->code = WfpError::None;
        status->detail.clear();
    }
    return true;
}

} // namespace drift::interchange::filmora

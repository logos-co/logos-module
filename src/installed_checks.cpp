// ModuleDirectory's checks. Split out so that only this object file references
// lgx: a consumer that merely opens a directory links no logos-package.
#include "module_directory.h"

#include <lgx.h>

#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>

namespace ModuleLib {

namespace {

QStringList toStringList(const char** array)
{
    QStringList out;
    if (!array) {
        return out;
    }
    for (const char** entry = array; *entry != nullptr; ++entry) {
        out.append(QString::fromUtf8(*entry));
    }
    return out;
}

IntegrityState toIntegrityState(lgx_integrity_t value)
{
    switch (value) {
    case LGX_INTEGRITY_OK:         return IntegrityState::Ok;
    case LGX_INTEGRITY_MISMATCH:   return IntegrityState::Mismatch;
    case LGX_INTEGRITY_NO_HASH:    return IntegrityState::NoHash;
    case LGX_INTEGRITY_UNREADABLE: return IntegrityState::Unreadable;
    case LGX_INTEGRITY_BAD_INPUT:  return IntegrityState::BadInput;
    }
    return IntegrityState::Unreadable;
}

SignatureCheck toSignatureCheck(lgx_sig_check_t value)
{
    switch (value) {
    case LGX_SIG_CHECK_OK:       return SignatureCheck::Ok;
    case LGX_SIG_CHECK_MISMATCH: return SignatureCheck::Mismatch;
    case LGX_SIG_CHECK_UNUSABLE: return SignatureCheck::Unusable;
    case LGX_SIG_CHECK_BAD_DID:  return SignatureCheck::BadDid;
    }
    return SignatureCheck::Unusable;
}

InstalledChecks cannotRun(const QString& why)
{
    InstalledChecks checks;
    checks.integrityDetail = why;
    checks.errors.append(why);
    return checks;
}

}  // namespace

InstalledChecks ModuleDirectory::checkInstalled() const
{
    // The directory itself first: "no manifest.json" is a strange thing to say
    // about a path that is a file, or is not there at all.
    if (m_directoryState == FileState::Absent) {
        return cannotRun(QStringLiteral("no such directory: %1").arg(m_path));
    }
    if (m_directoryState == FileState::Malformed) {
        return cannotRun(QStringLiteral("not a directory: %1").arg(m_path));
    }

    // Then the two inputs logos-package cannot be asked without. Reported from
    // here because this library knows WHICH one is wrong, where the ABI, given
    // only bytes, could say no more than "failed to parse manifest".
    switch (m_manifest.state) {
    case FileState::Absent:
        return cannotRun(QStringLiteral("no manifest.json in %1").arg(m_path));
    case FileState::Unreadable:
        return cannotRun(QStringLiteral("manifest.json could not be read: %1")
                             .arg(m_manifest.error));
    case FileState::Malformed:
        return cannotRun(QStringLiteral("manifest.json is not valid JSON: %1")
                             .arg(m_manifest.error));
    case FileState::Present:
        break;
    }

    const QString variant = checkedVariant();
    if (variant.isEmpty()) {
        return cannotRun(QStringLiteral(
            "no installed variant: %1 has no `variant` file and its manifest's main "
            "matched none of the candidate variants").arg(m_path));
    }

    InstalledChecks checks;
    checks.ran = true;
    checks.variant = variant;

    const QByteArray dirUtf8 = QDir::toNativeSeparators(m_path).toUtf8();
    const QByteArray variantUtf8 = variant.toUtf8();
    const char* manifestBytes = m_manifest.bytes.constData();
    const size_t manifestLen = static_cast<size_t>(m_manifest.bytes.size());

    // Two calls, and the second repeats the first's tree walk. Deliberate: a
    // host branches on Mismatch-versus-NoHash before it loads code, and the
    // only other way to recover that distinction from lgx_verify_installed
    // would be to parse its error strings.
    const lgx_integrity_t integrity = lgx_verify_installed_tree(
        dirUtf8.constData(), manifestBytes, manifestLen, variantUtf8.constData());
    checks.integrity = toIntegrityState(integrity);
    if (integrity != LGX_INTEGRITY_OK) {
        checks.integrityDetail = QString::fromUtf8(lgx_get_last_error());
    }

    lgx_verify_result_t result = lgx_verify_installed(
        dirUtf8.constData(), manifestBytes, manifestLen, variantUtf8.constData());
    checks.valid = result.valid;
    checks.errors = toStringList(result.errors);
    checks.warnings = toStringList(result.warnings);
    lgx_free_verify_result(result);

    return checks;
}

SignatureCheck ModuleDirectory::checkSignature(const QString& expectedDid) const
{
    // manifest.json is the signed message. Without it there is nothing to
    // verify a signature over, and calling anyway would answer Mismatch —
    // "someone else signed these bytes" — about bytes that do not exist.
    if (!m_manifest.isPresent()) {
        return SignatureCheck::NoMessage;
    }
    if (!m_signature.isPresent()) {
        return SignatureCheck::Unsigned;
    }

    const QByteArray sigJson = m_signature.bytes;
    const QByteArray did = expectedDid.toUtf8();
    return toSignatureCheck(lgx_check_manifest_signature(
        m_manifest.bytes.constData(), static_cast<size_t>(m_manifest.bytes.size()),
        sigJson.constData(), did.constData()));
}

QString ModuleDirectory::claimedSignerDid() const
{
    if (!m_signature.isPresent()) {
        return QString();
    }
    // A display field, read with a JSON parser rather than through lgx on
    // purpose: it must never travel anywhere a key could be derived from it.
    const QJsonDocument doc = QJsonDocument::fromJson(m_signature.bytes);
    if (!doc.isObject()) {
        return QString();
    }
    return doc.object().value(QStringLiteral("did")).toString();
}

}  // namespace ModuleLib

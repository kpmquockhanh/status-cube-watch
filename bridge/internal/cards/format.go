package cards

import (
	"math"
	"math/big"
	"regexp"
	"strconv"
	"strings"

	"golang.org/x/text/unicode/norm"
)

// toFixed is JS Number.prototype.toFixed for 0 <= d <= 20 and |x| < 1e21:
// it rounds the exact binary value of x, a tie going to the larger n.
func toFixed(x float64, d int) string {
	neg := x < 0
	if neg {
		x = -x
	}
	f := new(big.Float).SetPrec(256).SetFloat64(x)
	scale := new(big.Float).SetPrec(256).SetInt(new(big.Int).Exp(big.NewInt(10), big.NewInt(int64(d)), nil))
	f.Mul(f, scale)
	f.Add(f, big.NewFloat(0.5))
	n, _ := f.Int(nil) // truncates toward zero, which is floor for x >= 0
	digits := n.String()
	if len(digits) <= d {
		digits = strings.Repeat("0", d-len(digits)+1) + digits
	}
	s := digits
	if d > 0 {
		s = digits[:len(digits)-d] + "." + digits[len(digits)-d:]
	}
	if neg && n.Sign() != 0 {
		s = "-" + s
	}
	return s
}

// jsRound is Math.round: halves go up (toward +Infinity).
func jsRound(x float64) int64 { return int64(math.Floor(x + 0.5)) }

func itoa(n int64) string { return strconv.FormatInt(n, 10) }

func money(usd float64) string {
	if usd >= 1000 {
		d := 1
		if usd >= 10_000 {
			d = 0
		}
		return "$" + toFixed(usd/1000, d) + "k"
	}
	if usd >= 100 {
		return "$" + toFixed(usd, 0)
	}
	return "$" + toFixed(usd, 2)
}

func tokens(n int64) string {
	x := float64(n)
	switch {
	case x >= 1e9:
		return toFixed(x/1e9, 1) + "B"
	case x >= 1e6:
		return toFixed(x/1e6, 1) + "M"
	case x >= 1e3:
		return toFixed(x/1e3, 0) + "K"
	}
	return itoa(n)
}

func ago(ms int64) string {
	s := max(0, jsRound(float64(ms)/1000))
	switch {
	case s < 60:
		return itoa(s) + "s ago"
	case s < 3600:
		return itoa(jsRound(float64(s)/60)) + "m ago"
	case s < 86400:
		return itoa(jsRound(float64(s)/3600)) + "h ago"
	}
	return itoa(jsRound(float64(s)/86400)) + "d ago"
}

// until: "47m" / "4h 05m" / "16h" / "2d 3h". Precision drops as the horizon
// grows, and the string stays short enough for the middle of the rings.
func until(ms int64) string {
	m := max(0, jsRound(float64(ms)/60_000))
	if m < 60 {
		return itoa(m) + "m"
	}
	h := m / 60
	if h < 10 {
		return itoa(h) + "h " + pad2(m%60) + "m"
	}
	if h < 24 {
		return itoa(h) + "h"
	}
	return itoa(h/24) + "d " + itoa(h%24) + "h"
}

func pad2(n int64) string {
	if n < 10 {
		return "0" + itoa(n)
	}
	return itoa(n)
}

// thousands groups the digits of n by three with commas (en-US).
func thousands(n int) string {
	s := strconv.Itoa(n)
	sign := ""
	if n < 0 {
		sign, s = "-", s[1:]
	}
	var b strings.Builder
	for i, r := range s {
		if i > 0 && (len(s)-i)%3 == 0 {
			b.WriteByte(',')
		}
		b.WriteRune(r)
	}
	return sign + b.String()
}

var (
	modelPrefix  = regexp.MustCompile(`^claude-`)
	modelDate    = regexp.MustCompile(`-\d{8}$`)
	familyFirst  = regexp.MustCompile(`^([a-z]+)-(\d+)(?:-(\d{1,2}))?$`)
	versionFirst = regexp.MustCompile(`^(\d+)(?:-(\d{1,2}))?-([a-z]+)$`)
)

// prettyModel: "claude-haiku-4-5-20251001" -> "Haiku 4.5", "claude-opus-4-0"
// -> "Opus 4", "claude-3-5-haiku" -> "Haiku 3.5". Anything else just loses
// the prefix (and gets a capital). "" reads as "unknown".
func prettyModel(id string) string {
	if id == "" {
		id = "unknown"
	}
	name := modelDate.ReplaceAllString(modelPrefix.ReplaceAllString(id, ""), "")
	var family, major, minor string
	if m := familyFirst.FindStringSubmatch(name); m != nil {
		family, major, minor = m[1], m[2], m[3]
	} else if m := versionFirst.FindStringSubmatch(name); m != nil {
		family, major, minor = m[3], m[1], m[2]
	} else {
		// name.replace(/^(\w)/, upper): only an ASCII word character changes.
		if name != "" && isWordByte(name[0]) {
			return strings.ToUpper(name[:1]) + name[1:]
		}
		return name
	}
	version := major
	if minor != "" && minor != "0" {
		version = major + "." + minor
	}
	return strings.ToUpper(family[:1]) + family[1:] + " " + version
}

func isWordByte(c byte) bool {
	return c == '_' || '0' <= c && c <= '9' || 'a' <= c && c <= 'z' || 'A' <= c && c <= 'Z'
}

// Typographic characters the copy (or a transcript path) is likely to carry,
// and letters NFKD does not split into base + accent, mapped to an ASCII
// look-alike before everything else non-ASCII is dropped.
var asciiLookalike = strings.NewReplacer(
	"·", "-", "•", "-", "–", "-", "—", "-", "−", "-",
	"‘", "'", "’", "'", "“", `"`, "”", `"`,
	"…", "...", "×", "x",
	"đ", "d", "Đ", "D", "ø", "o", "Ø", "O", "ł", "l", "Ł", "L",
	"ß", "ss", "æ", "ae", "Æ", "AE",
)

// jsSpace is JS RegExp \s (which differs from unicode.IsSpace: it has U+FEFF
// and lacks U+0085).
func jsSpace(r rune) bool {
	switch r {
	case '\t', '\n', '\v', '\f', '\r', ' ', 0xa0, 0x1680, 0x2028, 0x2029, 0x202f, 0x205f, 0x3000, 0xfeff:
		return true
	}
	return 0x2000 <= r && r <= 0x200a
}

// Fit makes s printable ASCII (0x20-0x7E) at most bytes-1 long, as the
// firmware buffers (NUL included) need. Accents are stripped ("e" for "é"),
// whitespace becomes a space, the rest is dropped, trailing spaces trimmed.
func Fit(s string, bytes int) string {
	s = norm.NFKD.String(asciiLookalike.Replace(s))
	var b strings.Builder
	for _, r := range s {
		if jsSpace(r) {
			r = ' '
		}
		if r >= 0x20 && r <= 0x7e {
			b.WriteRune(r)
		}
	}
	out := b.String()
	if n := bytes - 1; len(out) > n {
		out = out[:max(0, n)]
	}
	return strings.TrimRight(out, " ")
}

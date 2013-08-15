#include <stdint.h>

class CubeHash {
public:
	unsigned char hashval[512/8];

	// tunable parameters as per Dan Bernstein's 2009-09 NIST submission
	CubeHash(unsigned int i, unsigned int r, unsigned int b, unsigned int f, unsigned int h) :
		initial_rounds(i),
		block_rounds(r),
		final_rounds(f),
		bytes_per_block(b),
		hashbytelen((h + 7) / 8),
		byte_pos(0)
	{
		Init();
	}

	void Update(const unsigned char *data, std::size_t databytelen)
	{
		while (databytelen > 0) {
			const uint32_t u(uint32_t(*data) << (8U * (byte_pos % 4U)));
			x[byte_pos / 4U] ^= u;
			++data;
			--databytelen;
			++byte_pos;
			if (byte_pos == bytes_per_block) {
				transform(block_rounds);
				byte_pos = 0U;
			}
		}
	}

	void Final()
	{
		const uint32_t u(uint32_t(128) << (8U * (byte_pos % 4U)));
		x[byte_pos / 4U] ^= u;
		transform(block_rounds);
		x[31U] ^= 1U;
		transform(final_rounds);
		for (unsigned i = 0;i < hashbytelen;++i)
			hashval[i] = static_cast<unsigned char>(x[i / 4U] >> (8U * (i % 4U)));
	}

protected:
	const unsigned int initial_rounds;
	const unsigned int block_rounds;
	const unsigned int final_rounds;
	const unsigned int bytes_per_block;
	const unsigned int hashbytelen;
	unsigned int byte_pos; ///< number of bytes read into x from current block
	uint32_t x[32];

	uint32_t ROTATE(uint32_t a, uint32_t b) { return (a << b) | (a >> (32U - b)); }

	void transform(unsigned int rounds)
	{
		enum { HALF_LENGTH = (sizeof x/sizeof *x) / 2 };
		uint32_t y[HALF_LENGTH];

		for (unsigned r = rounds;r > 0;--r) {
			for (unsigned i = 0;i < HALF_LENGTH;++i) x[i + HALF_LENGTH] += x[i];
			for (unsigned i = 0;i < HALF_LENGTH;++i) y[i ^ 8] = x[i];
			for (unsigned i = 0;i < HALF_LENGTH;++i) x[i] = ROTATE(y[i],7);
			for (unsigned i = 0;i < HALF_LENGTH;++i) x[i] ^= x[i + HALF_LENGTH];
			for (unsigned i = 0;i < HALF_LENGTH;++i) y[i ^ 2] = x[i + HALF_LENGTH];
			for (unsigned i = 0;i < HALF_LENGTH;++i) x[i + HALF_LENGTH] = y[i];
			for (unsigned i = 0;i < HALF_LENGTH;++i) x[i + HALF_LENGTH] += x[i];
			for (unsigned i = 0;i < HALF_LENGTH;++i) y[i ^ 4] = x[i];
			for (unsigned i = 0;i < HALF_LENGTH;++i) x[i] = ROTATE(y[i],11);
			for (unsigned i = 0;i < HALF_LENGTH;++i) x[i] ^= x[i + HALF_LENGTH];
			for (unsigned i = 0;i < HALF_LENGTH;++i) y[i ^ 1] = x[i + HALF_LENGTH];
			for (unsigned i = 0;i < HALF_LENGTH;++i) x[i + HALF_LENGTH] = y[i];
		}
	}

	void Init()
	{
		if (hashbytelen < 1 || hashbytelen > 64 || hashbytelen > (sizeof hashval/sizeof *hashval))
			throw "invalid hash length";
		if (!initial_rounds || !block_rounds || !final_rounds)
			throw "invalid number of rounds";
		if (bytes_per_block < 1 || bytes_per_block > (4U * (sizeof x/sizeof *x)))
			throw "invalid number of bytes per block";

		x[0] = hashbytelen;
		x[1] = bytes_per_block;
		x[2] = block_rounds;
		for (unsigned i = 3;i < (sizeof x/sizeof *x);++i) x[i] = 0;
		transform(initial_rounds);
	}
};

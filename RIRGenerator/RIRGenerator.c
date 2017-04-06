#include "RIRGenerator.h"
#include <stdio.h>
double Sinc(double x)
{
	if (x == 0)
		return(1.);
	else
		return(sin(x) / x);
}
double microphoneType(double x, double y, double z, double* angle, int mtype)
{
	double gain, vartheta, varphi, rho;
	// Polar Pattern         rho
	switch (mtype)
	{
	case 0:
		rho = 0; //Bidirectional 0
		break;
	case 1:
		rho = 0.25; //Hypercardioid 0.25
		break;
	case 2:
		rho = 0.5; //Cardioid 0.5
		break;
	case 3:
		rho = 0.75; //Subcardioid 0.75
		break;
	case 4:
		rho = 1; //Omnidirectional 1
		break;
	};
	vartheta = acos(z / sqrt(pow(x, 2) + pow(y, 2) + pow(z, 2)));
	varphi = atan2(y, x);
	gain = sin(M_PI / 2 - angle[1]) * sin(vartheta) * cos(angle[0] - varphi) + cos(M_PI / 2 - angle[1]) * cos(vartheta);
	gain = rho + (1 - rho) * gain;
	return gain;
}
double** rir_generator(double c, int fs, int nMicrophones, double** rr, double* ss, double* LL, int betaNum, double* beta, int nSamples, int microphone_type, int nOrder, int nDimension, int orientationEnable, double* orientation, int isHighPassFilter)
{
	int j;
	double reverberation_time = 0;;
	double angle[2];
	if (betaNum == 1)
	{
		double V = LL[0] * LL[1] * LL[2];
		double S = 2 * (LL[0] * LL[2] + LL[1] * LL[2] + LL[0] * LL[1]);
		reverberation_time = beta[0];
		if (reverberation_time != 0) {
			double alfa = 24 * V*log(10.0) / (c*S*reverberation_time);
			if (alfa > 1) {
				printf("Error: The reflection coefficients cannot be calculated using the current room parameters, i.e. room size and reverberation time.\nPlease specify the reflection coefficients or change the room parameters.");
				return 0;
		}
			for (int i = 0; i < 6; i++)
				beta[i] = sqrt(1 - alfa);
		}
		else
		{
			for (int i = 0; i < 6; i++)
				beta[i] = 0;
		}
	}
	if (orientationEnable == 1)
	{
		angle[0] = orientation[0];
		angle[1] = orientation[1];
	}
	else
	{
		angle[0] = 0;
		angle[1] = 0;
	}
	if (nDimension == 2)
	{
		beta[4] = 0;
		beta[5] = 0;
	}
	else
	{
		nDimension = 3;
	}
	if (nOrder < -1)
		nOrder = -1;
	if (nSamples == 0 && betaNum > 1)
	{
		double V = LL[0] * LL[1] * LL[2];
		double alpha = ((1 - pow(beta[0], 2)) + (1 - pow(beta[1], 2)))*LL[1] * LL[2] +
			((1 - pow(beta[2], 2)) + (1 - pow(beta[3], 2)))*LL[0] * LL[2] +
			((1 - pow(beta[4], 2)) + (1 - pow(beta[5], 2)))*LL[0] * LL[1];
		reverberation_time = 24 * log(10.0)*V / (c*alpha);
		if (reverberation_time < 0.128)
			reverberation_time = 0.128;
		nSamples = (int)(reverberation_time * fs);
	}
	// Create output vector
	double** imp = gen2DArrayCALLOC(nMicrophones, nSamples);
	// Temporary variables and constants (high-pass filter)
	const double W = 2 * M_PI * 100 / fs; // The cut-off frequency equals 100 Hz
	const double R1 = exp(-W);
	const double B1 = 2 * R1*cos(W);
	const double B2 = -R1 * R1;
	const double A1 = -(1 + R1);
	double       X0;
	double      Y[3];

	// Temporary variables and constants (image-method)
	const double Fc = 1; // The cut-off frequency equals fs/2 - Fc is the normalized cut-off frequency.
	const int    Tw = 2 * ROUND(0.004*fs); // The width of the low-pass FIR equals 8 ms
	const double cTs = c / fs;
	double* LPI = (double*)calloc(Tw, sizeof(double));
	double      r[3];
	double      s[3];
	double      L[3];
	double       Rm[3];
	double       Rp_plus_Rm[3];
	double       refl[3];
	double       fdist, dist;
	double       gain;
	int          startPosition;
	int          n1, n2, n3;
	int          q, k;
	int          mx, my, mz;
	int          n;

	s[0] = ss[0] / cTs; s[1] = ss[1] / cTs; s[2] = ss[2] / cTs;
	L[0] = LL[0] / cTs; L[1] = LL[1] / cTs; L[2] = LL[2] / cTs;
	for (int idxMicrophone = 0; idxMicrophone < nMicrophones; idxMicrophone++)
	{
		printf("\nProcessing microphone %d", idxMicrophone);
		r[0] = rr[idxMicrophone][0] / cTs;
		r[1] = rr[idxMicrophone][1] / cTs;
		r[2] = rr[idxMicrophone][2] / cTs;
		n1 = (int)ceil(nSamples / (2 * L[0]));
		n2 = (int)ceil(nSamples / (2 * L[1]));
		n3 = (int)ceil(nSamples / (2 * L[2]));

		// Generate room impulse response
		for (mx = -n1; mx <= n1; mx++)
		{
			Rm[0] = 2 * mx*L[0];

			for (my = -n2; my <= n2; my++)
			{
				Rm[1] = 2 * my*L[1];

				for (mz = -n3; mz <= n3; mz++)
				{
					Rm[2] = 2 * mz*L[2];

					for (q = 0; q <= 1; q++)
					{
						Rp_plus_Rm[0] = (1 - 2 * q)*s[0] - r[0] + Rm[0];
						refl[0] = pow(beta[0], abs(mx - q)) * pow(beta[1], abs(mx));

						for (j = 0; j <= 1; j++)
						{
							Rp_plus_Rm[1] = (1 - 2 * j)*s[1] - r[1] + Rm[1];
							refl[1] = pow(beta[2], abs(my - j)) * pow(beta[3], abs(my));

							for (k = 0; k <= 1; k++)
							{
								Rp_plus_Rm[2] = (1 - 2 * k)*s[2] - r[2] + Rm[2];
								refl[2] = pow(beta[4], abs(mz - k)) * pow(beta[5], abs(mz));

								dist = sqrt(pow(Rp_plus_Rm[0], 2) + pow(Rp_plus_Rm[1], 2) + pow(Rp_plus_Rm[2], 2));

								if (abs(2 * mx - q) + abs(2 * my - j) + abs(2 * mz - k) <= nOrder || nOrder == -1)
								{
									fdist = floor(dist);
									if (fdist < nSamples)
									{
										gain = microphoneType(Rp_plus_Rm[0], Rp_plus_Rm[1], Rp_plus_Rm[2], angle, microphone_type)
											* refl[0] * refl[1] * refl[2] / (4 * M_PI*dist*cTs);

										for (n = 0; n < Tw; n++)
											LPI[n] = 0.5 * (1 - cos(2 * M_PI*((n + 1 - (dist - fdist)) / Tw))) * Fc * Sinc(M_PI*Fc*(n + 1 - (dist - fdist) - (Tw / 2)));

										startPosition = (int)fdist - (Tw / 2) + 1;
										for (n = 0; n < Tw; n++)
											if (startPosition + n >= 0 && startPosition + n < nSamples)
												imp[idxMicrophone][(startPosition + n)] += gain * LPI[n];
									}
								}
							}
						}
					}
				}
			}
		}
		// 'Original' high-pass filter as proposed by Allen and Berkley.
		if (isHighPassFilter == 1)
		{
			for (int idx = 0; idx < 3; idx++) { Y[idx] = 0; }
			for (int idx = 0; idx < nSamples; idx++)
			{
				X0 = imp[idxMicrophone][idx];
				Y[2] = Y[1];
				Y[1] = Y[0];
				Y[0] = B1*Y[1] + B2*Y[2] + X0;
				imp[idxMicrophone][idx] = Y[0] + A1*Y[1] + R1*Y[2];
			}
		}
	}
	free(LPI);
	return imp;
}
double** gen2DArrayCALLOC(int arraySizeX, int arraySizeY) {
	double** array2D;
	array2D = (double**)calloc(arraySizeX, sizeof(double*));
	for (int i = 0; i < arraySizeX; i++)
		array2D[i] = (double*)calloc(arraySizeY, sizeof(double));
	return array2D;
}
double* gen1DArrayCALLOC(int arraySize) {
	double* array1D;
	array1D = (double*)calloc(arraySize, sizeof(double*));
	return array1D;
}